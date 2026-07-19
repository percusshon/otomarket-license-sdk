#include <otomarket/license/LicenseSdk.h>

#include "LicenseInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern "C" {
#include "tweetnacl.h"
}

namespace otomarket::license {
namespace {

using namespace detail;

std::string jsonEscape(const std::string& value) {
  std::ostringstream output;

  for (const unsigned char ch : value) {
    switch (ch) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (ch < 0x20) {
          output << "\\u"
                 << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(ch)
                 << std::dec << std::setfill(' ');
        } else {
          output << static_cast<char>(ch);
        }
        break;
    }
  }

  return output.str();
}

std::string quoteJson(const std::string& value) {
  return "\"" + jsonEscape(value) + "\"";
}

std::string makeJsonObject(const std::vector<std::pair<std::string, std::string>>& fields) {
  std::ostringstream output;
  output << "{";

  for (size_t index = 0; index < fields.size(); ++index) {
    if (index != 0) {
      output << ",";
    }
    output << quoteJson(fields[index].first) << ":" << fields[index].second;
  }

  output << "}";
  return output.str();
}

std::string hexEncode(const unsigned char* bytes, size_t length) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');

  for (size_t index = 0; index < length; ++index) {
    output << std::setw(2) << static_cast<int>(bytes[index]);
  }

  return output.str();
}

std::string sha512TruncatedHex(const std::string& input, size_t bytesToKeep) {
  std::array<unsigned char, crypto_hash_BYTES> digest{};
  crypto_hash(
    digest.data(),
    reinterpret_cast<const unsigned char*>(input.data()),
    static_cast<unsigned long long>(input.size())
  );
  return hexEncode(digest.data(), std::min(bytesToKeep, digest.size()));
}

std::optional<std::string> readFirstLine(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return std::nullopt;
  }

  std::string line;
  std::getline(input, line);
  line = trim(line);
  if (line.empty()) {
    return std::nullopt;
  }

  return line;
}

std::optional<std::string> environmentVariable(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || std::strlen(value) == 0) {
    return std::nullopt;
  }
  return std::string(value);
}

std::string hostName() {
#ifdef _WIN32
  char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD size = sizeof(buffer);
  if (GetComputerNameA(buffer, &size) != 0) {
    return std::string(buffer, size);
  }
  return {};
#else
  char buffer[256] = {};
  if (gethostname(buffer, sizeof(buffer) - 1) == 0) {
    return std::string(buffer);
  }
  return {};
#endif
}

std::string formatIso8601(std::chrono::system_clock::time_point value) {
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
    value.time_since_epoch()
  ) % 1000;
  const std::time_t seconds = std::chrono::system_clock::to_time_t(value);
  const std::tm time = gmtimePortable(seconds);

  std::ostringstream output;
  output << std::put_time(&time, "%Y-%m-%dT%H:%M:%S")
         << "."
         << std::setw(3) << std::setfill('0') << milliseconds.count()
         << "Z";
  return output.str();
}

std::optional<ErrorCode> knownApiErrorCode(const std::string& error) {
  if (error == "SEAT_LIMIT") {
    return ErrorCode::SeatLimit;
  }
  if (error == "EXPIRED") {
    return ErrorCode::Expired;
  }
  if (error == "REVOKED") {
    return ErrorCode::Revoked;
  }
  if (error == "NOT_FOUND") {
    return ErrorCode::NotFound;
  }
  if (error == "PRODUCT_MISMATCH") {
    return ErrorCode::ProductMismatch;
  }
  if (error == "TRIAL_NOT_AVAILABLE") {
    return ErrorCode::TrialNotAvailable;
  }
  if (error == "TRIAL_EXPIRED") {
    return ErrorCode::TrialExpired;
  }
  return std::nullopt;
}

ErrorCode apiErrorCode(const std::string& error) {
  return knownApiErrorCode(error).value_or(ErrorCode::InvalidResponse);
}

LicenseState stateForError(ErrorCode error) {
  switch (error) {
    case ErrorCode::SeatLimit:
      return LicenseState::SeatLimit;
    case ErrorCode::Expired:
      return LicenseState::Expired;
    case ErrorCode::Revoked:
      return LicenseState::Revoked;
    case ErrorCode::NotFound:
      return LicenseState::NotFound;
    case ErrorCode::ProductMismatch:
      return LicenseState::ProductMismatch;
    case ErrorCode::TrialExpired:
      return LicenseState::Expired;
    case ErrorCode::NetworkError:
      return LicenseState::NetworkUnavailable;
    case ErrorCode::RequestRejected:
      return LicenseState::Error;
    case ErrorCode::InvalidSignature:
    case ErrorCode::InvalidCache:
    case ErrorCode::MachineMismatch:
      return LicenseState::InvalidCache;
    default:
      return LicenseState::Error;
  }
}

std::optional<LicenseInfo> parseLicensePayload(
  const std::string& payloadJson,
  ErrorCode& error,
  std::string& message
) {
  JsonValue payload;

  try {
    payload = JsonParser(payloadJson).parse();
  } catch (const std::exception& exception) {
    error = ErrorCode::InvalidSignature;
    message = exception.what();
    return std::nullopt;
  }

  const auto licenseId = stringField(payload, "licenseId");
  const auto productId = stringField(payload, "productId");
  const auto buyerId = stringField(payload, "buyerId");
  const auto machineId = stringField(payload, "machineId");
  const auto maxActivations = intField(payload, "maxActivations");
  const auto issuedAt = stringField(payload, "issuedAt");
  const auto verifyAfter = stringField(payload, "verifyAfter");

  if (!licenseId || !productId || !buyerId || !machineId ||
      !maxActivations || !issuedAt || !verifyAfter) {
    error = ErrorCode::InvalidSignature;
    message = "Signed license payload is missing a required field.";
    return std::nullopt;
  }

  LicenseInfo info;
  info.licenseId = *licenseId;
  info.productId = *productId;
  info.buyerId = *buyerId;
  info.machineId = *machineId;
  info.maxActivations = *maxActivations;
  info.isTrial = boolField(payload, "isTrial").value_or(false);

  try {
    info.issuedAt = parseIso8601(*issuedAt);
    info.verifyAfter = parseIso8601(*verifyAfter);

    if (const auto expiresAt = stringField(payload, "expiresAt")) {
      info.expiresAt = parseIso8601(*expiresAt);
    } else if (!isNullField(payload, "expiresAt")) {
      error = ErrorCode::InvalidSignature;
      message = "Signed license payload expiresAt must be a string or null.";
      return std::nullopt;
    }
  } catch (const std::exception& exception) {
    error = ErrorCode::InvalidSignature;
    message = exception.what();
    return std::nullopt;
  }

  return info;
}

std::optional<JsonValue> parseJsonObject(const std::string& body) {
  try {
    JsonValue value = JsonParser(body).parse();
    if (value.type != JsonValue::Type::Object) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

struct HttpFailureClassification {
  ErrorCode error = ErrorCode::NetworkError;
  LicenseState state = LicenseState::NetworkUnavailable;
  std::string message;
};

std::string httpFailureMessage(
  const std::string& operation,
  int statusCode
) {
  return operation + " request failed with HTTP " + std::to_string(statusCode) + ".";
}

std::string rejectedRequestMessage(
  const std::string& serverMessage,
  int statusCode
) {
  return serverMessage + " (HTTP " + std::to_string(statusCode) + ").";
}

HttpFailureClassification classifyHttpFailure(
  const HttpResponse& response,
  const std::string& operation
) {
  HttpFailureClassification failure;
  failure.message = httpFailureMessage(operation, response.statusCode);

  const auto json = parseJsonObject(response.body);
  if (!json) {
    return failure;
  }

  auto code = stringField(*json, "code");
  if (!code) {
    code = stringField(*json, "error");
  }

  if (!code || code->empty()) {
    return failure;
  }

  const auto serverError = stringField(*json, "error");
  const std::string serverMessage =
    serverError && !serverError->empty() ? *serverError : *code;

  if (const auto knownError = knownApiErrorCode(*code)) {
    failure.error = *knownError;
    failure.state = stateForError(*knownError);
    failure.message = serverMessage;
    return failure;
  }

  failure.error = ErrorCode::RequestRejected;
  failure.state = LicenseState::Error;
  failure.message = rejectedRequestMessage(serverMessage, response.statusCode);
  return failure;
}

constexpr const char* kMissingKidErrorMessage = "License token kid was not found in keyset.";

struct KeysetCacheRecord {
  std::map<std::string, std::string> keys;
  std::chrono::system_clock::time_point fetchedAt{};
};

std::optional<std::map<std::string, std::string>> parseKeysetObject(const JsonValue& keysValue) {
  if (keysValue.type != JsonValue::Type::Object) {
    return std::nullopt;
  }

  std::map<std::string, std::string> keys;
  for (const auto& entry : keysValue.objectValue) {
    if (entry.first.empty() || entry.second.type != JsonValue::Type::String ||
        entry.second.stringValue.empty()) {
      return std::nullopt;
    }
    keys[entry.first] = entry.second.stringValue;
  }
  return keys;
}

std::optional<std::map<std::string, std::string>> parseRemoteKeyset(const std::string& body) {
  const auto json = parseJsonObject(body);
  if (!json) {
    return std::nullopt;
  }

  const JsonValue* keysField = objectField(*json, "keys");
  if (keysField == nullptr || keysField->type != JsonValue::Type::Array) {
    return std::nullopt;
  }

  std::map<std::string, std::string> keys;
  for (const auto& item : keysField->arrayValue) {
    if (item.type != JsonValue::Type::Object) {
      return std::nullopt;
    }

    const auto kid = stringField(item, "kid");
    const auto publicKeyPem = stringField(item, "publicKeyPem");
    if (!kid || kid->empty() || !publicKeyPem || publicKeyPem->empty()) {
      return std::nullopt;
    }

    keys[*kid] = *publicKeyPem;
  }

  return keys;
}

std::filesystem::path keysetCachePathFor(const std::filesystem::path& cachePath) {
  if (cachePath.empty()) {
    return {};
  }

  auto path = cachePath;
  path += ".keys";
  return path;
}

std::filesystem::path uniqueTemporaryPath(const std::filesystem::path& path) {
  static std::atomic<unsigned long long> counter{0};
  auto temporaryPath = path;
#ifdef _WIN32
  const auto processId = static_cast<unsigned long long>(GetCurrentProcessId());
#else
  const auto processId = static_cast<unsigned long long>(getpid());
#endif
  temporaryPath += ".tmp." + std::to_string(processId) + "." +
    std::to_string(counter.fetch_add(1));
  return temporaryPath;
}

bool writeFileAtomically(const std::filesystem::path& path, const std::string& body) {
  const auto temporaryPath = uniqueTemporaryPath(path);
  try {
#ifdef _WIN32
    // Windows relies on the ACL of the caller-selected user profile directory.
    {
      std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
      if (!output || !(output << body)) {
        std::filesystem::remove(temporaryPath);
        return false;
      }
    }
    if (!MoveFileExW(
          temporaryPath.c_str(),
          path.c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        )) {
      std::filesystem::remove(temporaryPath);
      return false;
    }
#else
    const int descriptor = ::open(
      temporaryPath.c_str(),
      O_CREAT | O_EXCL | O_WRONLY,
      S_IRUSR | S_IWUSR
    );
    if (descriptor < 0) {
      return false;
    }

    size_t written = 0;
    while (written < body.size()) {
      const auto count = ::write(descriptor, body.data() + written, body.size() - written);
      if (count < 0) {
        const int savedError = errno;
        ::close(descriptor);
        std::filesystem::remove(temporaryPath);
        errno = savedError;
        return false;
      }
      written += static_cast<size_t>(count);
    }
    if (::close(descriptor) != 0) {
      std::filesystem::remove(temporaryPath);
      return false;
    }
    std::filesystem::rename(temporaryPath, path);
#endif
    return true;
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporaryPath, ignored);
    return false;
  }
}

std::optional<KeysetCacheRecord> parseKeysetCacheBody(const std::string& body) {
  const auto json = parseJsonObject(body);
  if (!json) {
    return std::nullopt;
  }

  const JsonValue* keysField = objectField(*json, "keys");
  if (keysField == nullptr) {
    return std::nullopt;
  }

  auto keys = parseKeysetObject(*keysField);
  if (!keys) {
    return std::nullopt;
  }

  const JsonValue* fetchedAtField = objectField(*json, "fetchedAt");
  if (fetchedAtField == nullptr || fetchedAtField->type != JsonValue::Type::Number ||
      fetchedAtField->numberValue < 0) {
    return std::nullopt;
  }

  KeysetCacheRecord record;
  record.keys = std::move(*keys);
  record.fetchedAt = std::chrono::system_clock::from_time_t(
    static_cast<std::time_t>(fetchedAtField->numberValue)
  );
  return record;
}

std::optional<KeysetCacheRecord> readKeysetCacheFile(const std::filesystem::path& path) {
  try {
    if (path.empty()) {
      return std::nullopt;
    }

    std::ifstream input(path);
    if (!input) {
      return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseKeysetCacheBody(buffer.str());
  } catch (...) {
    return std::nullopt;
  }
}

bool writeKeysetCacheFile(
  const std::filesystem::path& path,
  const KeysetCacheRecord& record
) {
  try {
    if (path.empty()) {
      return false;
    }

    const auto parent = path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }

    std::ostringstream output;
    output << "{\"keys\":{";
    bool first = true;
    for (const auto& key : record.keys) {
      if (!first) {
        output << ",";
      }
      first = false;
      output << quoteJson(key.first) << ":" << quoteJson(key.second);
    }
    output << "},\"fetchedAt\":"
           << static_cast<long long>(std::chrono::system_clock::to_time_t(record.fetchedAt))
           << "}";
    return writeFileAtomically(path, output.str());
  } catch (...) {
    return false;
  }
}

void mergeKeyset(
  std::map<std::string, std::string>& target,
  const std::map<std::string, std::string>& source,
  bool overwrite
) {
  for (const auto& entry : source) {
    if (overwrite || target.find(entry.first) == target.end()) {
      target[entry.first] = entry.second;
    }
  }
}

bool isMissingKidFailure(const VerifyTokenResult& result) {
  return !result.ok &&
         result.error == ErrorCode::InvalidSignature &&
         result.errorMessage == kMissingKidErrorMessage;
}

bool httpOk(int statusCode) {
  return statusCode >= 200 && statusCode < 300;
}

std::string trimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

} // namespace

struct Client::CacheRecord {
  std::string productId;
  std::string licenseKey;
  std::string machineId;
  std::string licenseToken;
};

std::string toString(ErrorCode code) {
  switch (code) {
    case ErrorCode::None:
      return "NONE";
    case ErrorCode::SeatLimit:
      return "SEAT_LIMIT";
    case ErrorCode::Expired:
      return "EXPIRED";
    case ErrorCode::Revoked:
      return "REVOKED";
    case ErrorCode::NotFound:
      return "NOT_FOUND";
    case ErrorCode::ProductMismatch:
      return "PRODUCT_MISMATCH";
    case ErrorCode::TrialNotAvailable:
      return "TRIAL_NOT_AVAILABLE";
    case ErrorCode::TrialExpired:
      return "TRIAL_EXPIRED";
    case ErrorCode::NetworkError:
      return "NETWORK_ERROR";
    case ErrorCode::InvalidResponse:
      return "INVALID_RESPONSE";
    case ErrorCode::InvalidSignature:
      return "INVALID_SIGNATURE";
    case ErrorCode::InvalidCache:
      return "INVALID_CACHE";
    case ErrorCode::CacheIoError:
      return "CACHE_IO_ERROR";
    case ErrorCode::CacheMissing:
      return "CACHE_MISSING";
    case ErrorCode::MachineMismatch:
      return "MACHINE_MISMATCH";
    case ErrorCode::ConfigError:
      return "CONFIG_ERROR";
    case ErrorCode::RequestRejected:
      return "REQUEST_REJECTED";
  }

  return "UNKNOWN";
}

std::string toString(LicenseState state) {
  switch (state) {
    case LicenseState::Unknown:
      return "UNKNOWN";
    case LicenseState::Unlicensed:
      return "UNLICENSED";
    case LicenseState::Licensed:
      return "LICENSED";
    case LicenseState::LicensedOfflineGrace:
      return "LICENSED_OFFLINE_GRACE";
    case LicenseState::VerifyDue:
      return "VERIFY_DUE";
    case LicenseState::Expired:
      return "EXPIRED";
    case LicenseState::Revoked:
      return "REVOKED";
    case LicenseState::NotFound:
      return "NOT_FOUND";
    case LicenseState::ProductMismatch:
      return "PRODUCT_MISMATCH";
    case LicenseState::SeatLimit:
      return "SEAT_LIMIT";
    case LicenseState::InvalidCache:
      return "INVALID_CACHE";
    case LicenseState::NetworkUnavailable:
      return "NETWORK_UNAVAILABLE";
    case LicenseState::Error:
      return "ERROR";
  }

  return "UNKNOWN";
}

std::string defaultMachineFingerprint() {
  std::vector<std::string> parts;

  if (const auto machineId = readFirstLine("/etc/machine-id")) {
    parts.push_back("machine-id=" + *machineId);
  }

  if (const auto dbusMachineId = readFirstLine("/var/lib/dbus/machine-id")) {
    parts.push_back("dbus-machine-id=" + *dbusMachineId);
  }

  if (const std::string host = hostName(); !host.empty()) {
    parts.push_back("host=" + host);
  }

  if (const auto computerName = environmentVariable("COMPUTERNAME")) {
    parts.push_back("computer=" + *computerName);
  }

  if (const auto user = environmentVariable("USER")) {
    parts.push_back("user=" + *user);
  } else if (const auto username = environmentVariable("USERNAME")) {
    parts.push_back("user=" + *username);
  }

  if (const auto home = environmentVariable("HOME")) {
    parts.push_back("home=" + *home);
  } else if (const auto profile = environmentVariable("USERPROFILE")) {
    parts.push_back("home=" + *profile);
  }

  if (parts.empty()) {
    return "unknown";
  }

  std::ostringstream output;
  for (const auto& part : parts) {
    output << part << "\n";
  }
  return output.str();
}

std::string deriveMachineId(const std::string& productId, const std::string& machineFingerprint) {
  std::string material = "otomarket-license-sdk-v1";
  material.push_back('\0');
  material += productId;
  material.push_back('\0');
  material += machineFingerprint;
  return "oto2_" + sha512TruncatedHex(material, 32);
}

std::filesystem::path defaultCachePath(const std::string& appName) {
  const std::string safeName = appName.empty() ? "default" : appName;

#ifdef _WIN32
  if (const auto appData = environmentVariable("APPDATA")) {
    return std::filesystem::path(*appData) / "OtoMarketLicense" / safeName / "license.json";
  }
  return std::filesystem::path(".") / "OtoMarketLicense" / safeName / "license.json";
#elif defined(__APPLE__)
  if (const auto home = environmentVariable("HOME")) {
    return std::filesystem::path(*home) / "Library" / "Application Support" /
           "OtoMarketLicense" / safeName / "license.json";
  }
  return std::filesystem::path(".") / "OtoMarketLicense" / safeName / "license.json";
#else
  if (const auto configHome = environmentVariable("XDG_CONFIG_HOME")) {
    return std::filesystem::path(*configHome) / "OtoMarketLicense" / safeName / "license.json";
  }
  if (const auto home = environmentVariable("HOME")) {
    return std::filesystem::path(*home) / ".config" / "OtoMarketLicense" /
           safeName / "license.json";
  }
  return std::filesystem::path(".") / "OtoMarketLicense" / safeName / "license.json";
#endif
}

VerifyTokenResult verifyLicenseToken(
  const std::string& token,
  const std::string& publicKeyPem,
  const std::string& expectedProductId,
  const std::string& expectedMachineId,
  std::optional<std::chrono::system_clock::time_point> now,
  const std::map<std::string, std::string>& keyset
) {
  VerifyTokenResult result;
  if (token.size() > kMaxTokenEncodedSize) {
    result.error = ErrorCode::InvalidSignature;
    result.errorMessage = "License token exceeds the maximum supported size.";
    return result;
  }

  const std::vector<std::string> parts = splitToken(token);

  if (!tokenEncodedSizesAreValid(token, parts) ||
      parts[0].empty() || parts[1].empty() || parts[2].empty()) {
    result.error = ErrorCode::InvalidSignature;
    result.errorMessage = "License token must use header.payload.signature format.";
    return result;
  }

  JsonValue header;
  std::string payloadJson;
  std::vector<unsigned char> signature;

  try {
    const std::vector<unsigned char> headerBytes = base64Decode(parts[0]);
    const std::vector<unsigned char> payloadBytes = base64Decode(parts[1]);
    signature = base64Decode(parts[2]);
    header = JsonParser(std::string(headerBytes.begin(), headerBytes.end())).parse();
    payloadJson = std::string(payloadBytes.begin(), payloadBytes.end());
  } catch (const std::exception& exception) {
    result.error = ErrorCode::InvalidSignature;
    result.errorMessage = exception.what();
    return result;
  }

  const auto alg = stringField(header, "alg");
  const auto typ = stringField(header, "typ");

  if (!alg || *alg != "EdDSA" || (typ && *typ != "JWT")) {
    result.error = ErrorCode::InvalidSignature;
    result.errorMessage = "License token header is not EdDSA/JWT.";
    return result;
  }

  const std::string* selectedPublicKeyPem = &publicKeyPem;
  if (const JsonValue* kidField = objectField(header, "kid")) {
    if (kidField->type != JsonValue::Type::String) {
      result.error = ErrorCode::InvalidSignature;
      result.errorMessage = "License token header kid must be a string.";
      return result;
    }

    const auto found = keyset.find(kidField->stringValue);
    if (found == keyset.end()) {
      result.error = ErrorCode::InvalidSignature;
      result.errorMessage = kMissingKidErrorMessage;
      return result;
    }

    selectedPublicKeyPem = &found->second;
  }

  std::array<unsigned char, 32> publicKey{};
  try {
    publicKey = parsePublicKey(*selectedPublicKeyPem);
  } catch (const std::exception& exception) {
    result.error = ErrorCode::InvalidSignature;
    result.errorMessage = exception.what();
    return result;
  }

  const std::string signingInput = parts[0] + "." + parts[1];
  if (!verifyEd25519Detached(signingInput, signature, publicKey)) {
    result.error = ErrorCode::InvalidSignature;
    result.errorMessage = "License token signature did not verify.";
    return result;
  }

  ErrorCode payloadError = ErrorCode::None;
  std::string payloadMessage;
  auto license = parseLicensePayload(payloadJson, payloadError, payloadMessage);

  if (!license) {
    result.error = payloadError;
    result.errorMessage = payloadMessage;
    return result;
  }

  if (!expectedProductId.empty() && license->productId != expectedProductId) {
    result.error = ErrorCode::ProductMismatch;
    result.errorMessage = "License token productId does not match.";
    return result;
  }

  if (!expectedMachineId.empty() && license->machineId != expectedMachineId) {
    result.error = ErrorCode::MachineMismatch;
    result.errorMessage = "License token machineId does not match.";
    return result;
  }

  if (now && license->issuedAt > *now + std::chrono::hours(24)) {
    result.error = ErrorCode::InvalidSignature;
    result.errorMessage = "License token issuedAt is too far in the future.";
    return result;
  }

  if (now && license->expiresAt && *license->expiresAt <= *now) {
    result.error = ErrorCode::Expired;
    result.errorMessage = "License token is expired.";
    return result;
  }

  result.ok = true;
  result.license = license;
  return result;
}

Client::Client(Config config)
  : config_(std::move(config)) {
  if (!config_.clock) {
    config_.clock = [] {
      return std::chrono::system_clock::now();
    };
  }
  loadKeysetCache();
}

ActivateResult Client::otoActivate(
  const std::string& productId,
  const std::string& licenseKey,
  const std::string& machineName
) {
  ActivateResult result;

  if (productId.empty() || licenseKey.empty() || config_.baseUrl.empty() || !config_.http) {
    result.error = ErrorCode::ConfigError;
    result.errorMessage = "productId, licenseKey, baseUrl, and http transport are required.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  const std::string machineId = machineIdForProduct(productId);
  std::vector<std::pair<std::string, std::string>> fields = {
    {"productKey", quoteJson(productId)},
    {"licenseKey", quoteJson(licenseKey)},
    {"machineId", quoteJson(machineId)},
  };
  if (!machineName.empty()) {
    fields.emplace_back("machineName", quoteJson(machineName));
  }

  const std::string body = makeJsonObject(fields);

  HttpResponse response;
  try {
    response = config_.http->postJson(endpointUrl("activate"), body, {
      {"Content-Type", "application/json"},
      {"Accept", "application/json"},
    });
  } catch (const std::exception& exception) {
    result.error = ErrorCode::NetworkError;
    result.errorMessage = exception.what();
    setState(LicenseState::NetworkUnavailable, result.error);
    return result;
  }

  if (!httpOk(response.statusCode)) {
    const auto failure = classifyHttpFailure(response, "Activation");
    result.error = failure.error;
    result.errorMessage = failure.message;
    setState(failure.state, result.error);
    return result;
  }

  const auto json = parseJsonObject(response.body);
  if (!json) {
    result.error = ErrorCode::InvalidResponse;
    result.errorMessage = "Activation response was not valid JSON.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  const auto ok = boolField(*json, "ok");
  if (!ok) {
    result.error = ErrorCode::InvalidResponse;
    result.errorMessage = "Activation response omitted ok.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  if (!*ok) {
    const auto error = stringField(*json, "error").value_or("INVALID_RESPONSE");
    result.error = apiErrorCode(error);
    result.errorMessage = error;
    setState(stateForError(result.error), result.error);
    return result;
  }

  const auto licenseToken = stringField(*json, "license");
  if (!licenseToken) {
    result.error = ErrorCode::InvalidResponse;
    result.errorMessage = "Activation response omitted license.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  auto verified = verifyTokenWithKeysetRefresh(
    *licenseToken,
    productId,
    machineId,
    now()
  );
  if (!verified.ok || !verified.license) {
    result.error = verified.error;
    result.errorMessage = verified.errorMessage;
    setState(stateForError(result.error), result.error);
    return result;
  }

  CacheRecord cache;
  cache.productId = productId;
  cache.licenseKey = licenseKey;
  cache.machineId = machineId;
  cache.licenseToken = *licenseToken;

  std::string cacheMessage;
  if (!writeCache(cache, cacheMessage)) {
    result.error = ErrorCode::CacheIoError;
    result.errorMessage = cacheMessage;
    setState(LicenseState::Error, result.error);
    return result;
  }

  result.ok = true;
  result.licenseToken = *licenseToken;
  result.seatsUsed = intField(*json, "seatsUsed").value_or(0);
  result.maxActivations = intField(*json, "maxActivations").value_or(verified.license->maxActivations);
  result.expiresAt = verified.license->expiresAt;
  cachedLicense_ = verified.license;
  setState(LicenseState::Licensed);
  refreshKeysetIfDue();
  return result;
}

ActivateResult Client::otoStartTrial(const std::string& productId) {
  ActivateResult result;

  if (productId.empty() || config_.baseUrl.empty() || !config_.http) {
    result.error = ErrorCode::ConfigError;
    result.errorMessage = "productId, baseUrl, and http transport are required.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  const std::string machineId = machineIdForProduct(productId);
  const std::string body = makeJsonObject({
    {"productKey", quoteJson(productId)},
    {"machineId", quoteJson(machineId)},
  });

  HttpResponse response;
  try {
    response = config_.http->postJson(endpointUrl("trial"), body, {
      {"Content-Type", "application/json"},
      {"Accept", "application/json"},
    });
  } catch (const std::exception& exception) {
    result.error = ErrorCode::NetworkError;
    result.errorMessage = exception.what();
    setState(LicenseState::NetworkUnavailable, result.error);
    return result;
  }

  if (!httpOk(response.statusCode)) {
    const auto failure = classifyHttpFailure(response, "Trial");
    result.error = failure.error;
    result.errorMessage = failure.message;
    setState(failure.state, result.error);
    return result;
  }

  const auto json = parseJsonObject(response.body);
  if (!json) {
    result.error = ErrorCode::InvalidResponse;
    result.errorMessage = "Trial response was not valid JSON.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  const auto ok = boolField(*json, "ok");
  if (!ok) {
    result.error = ErrorCode::InvalidResponse;
    result.errorMessage = "Trial response omitted ok.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  if (!*ok) {
    const auto error = stringField(*json, "error").value_or("INVALID_RESPONSE");
    result.error = apiErrorCode(error);
    result.errorMessage = error;
    setState(stateForError(result.error), result.error);
    return result;
  }

  const auto licenseToken = stringField(*json, "license");
  if (!licenseToken) {
    result.error = ErrorCode::InvalidResponse;
    result.errorMessage = "Trial response omitted license.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  auto verified = verifyTokenWithKeysetRefresh(
    *licenseToken,
    productId,
    machineId,
    now()
  );
  if (!verified.ok || !verified.license) {
    result.error = verified.error;
    result.errorMessage = verified.errorMessage;
    setState(stateForError(result.error), result.error);
    return result;
  }

  CacheRecord cache;
  cache.productId = productId;
  cache.machineId = machineId;
  cache.licenseToken = *licenseToken;

  std::string cacheMessage;
  if (!writeCache(cache, cacheMessage)) {
    result.error = ErrorCode::CacheIoError;
    result.errorMessage = cacheMessage;
    setState(LicenseState::Error, result.error);
    return result;
  }

  result.ok = true;
  result.licenseToken = *licenseToken;
  result.seatsUsed = intField(*json, "seatsUsed").value_or(0);
  result.maxActivations = intField(*json, "maxActivations").value_or(verified.license->maxActivations);
  result.expiresAt = verified.license->expiresAt;
  cachedLicense_ = verified.license;
  setState(LicenseState::Licensed);
  refreshKeysetIfDue();
  return result;
}

bool Client::otoIsLicensed() {
  ErrorCode cacheError = ErrorCode::None;
  std::string cacheMessage;
  auto cache = readCache(cacheError, cacheMessage);

  if (!cache) {
    setState(cacheError == ErrorCode::CacheMissing ? LicenseState::Unlicensed : LicenseState::InvalidCache, cacheError);
    cachedLicense_.reset();
    return false;
  }

  const std::string expectedProductId =
    config_.expectedProductId.empty() ? cache->productId : config_.expectedProductId;
  const auto currentTime = now();

  auto verified = verifyTokenWithKeysetRefresh(
    cache->licenseToken,
    expectedProductId,
    cache->machineId,
    currentTime
  );

  if (!verified.ok || !verified.license) {
    setState(stateForError(verified.error), verified.error);
    cachedLicense_.reset();
    return false;
  }

  cachedLicense_ = verified.license;
  const auto effectiveVerifyAfter = std::min(
    verified.license->verifyAfter,
    verified.license->issuedAt + config_.maxOffline
  );

  if (verified.license->expiresAt && *verified.license->expiresAt <= currentTime) {
    setState(LicenseState::Expired, ErrorCode::Expired);
    return false;
  }

  if (currentTime < effectiveVerifyAfter) {
    setState(LicenseState::Licensed);
    return true;
  }

  setState(LicenseState::VerifyDue);

  LicenseInfo refreshed;
  if (tryOnlineVerify(*cache, refreshed)) {
    cachedLicense_ = refreshed;
    setState(LicenseState::Licensed);
    return true;
  }

  if (lastError_ == ErrorCode::NetworkError) {
    const auto graceDeadline = effectiveVerifyAfter + config_.verifyRetryGrace;

    if (currentTime <= graceDeadline) {
      setState(LicenseState::LicensedOfflineGrace, ErrorCode::None);
      return true;
    }
  }

  return false;
}

DeactivateResult Client::otoDeactivate() {
  DeactivateResult result;

  ErrorCode cacheError = ErrorCode::None;
  std::string cacheMessage;
  auto cache = readCache(cacheError, cacheMessage);

  if (!cache) {
    result.error = cacheError;
    result.errorMessage = cacheMessage;
    setState(LicenseState::Unlicensed, result.error);
    return result;
  }

  if (config_.baseUrl.empty() || !config_.http) {
    result.error = ErrorCode::ConfigError;
    result.errorMessage = "baseUrl and http transport are required.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  const std::string body = makeJsonObject({
    {"productKey", quoteJson(cache->productId)},
    {"licenseKey", quoteJson(cache->licenseKey)},
    {"machineId", quoteJson(cache->machineId)},
  });

  HttpResponse response;
  try {
    response = config_.http->postJson(endpointUrl("deactivate"), body, {
      {"Content-Type", "application/json"},
      {"Accept", "application/json"},
    });
  } catch (const std::exception& exception) {
    result.error = ErrorCode::NetworkError;
    result.errorMessage = exception.what();
    setState(LicenseState::NetworkUnavailable, result.error);
    return result;
  }

  if (!httpOk(response.statusCode)) {
    const auto failure = classifyHttpFailure(response, "Deactivate");
    result.error = failure.error;
    result.errorMessage = failure.message;
    setState(failure.state, result.error);
    return result;
  }

  const auto json = parseJsonObject(response.body);
  if (!json) {
    result.error = ErrorCode::InvalidResponse;
    result.errorMessage = "Deactivate response was not valid JSON.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  const auto ok = boolField(*json, "ok");
  if (!ok) {
    result.error = ErrorCode::InvalidResponse;
    result.errorMessage = "Deactivate response omitted ok.";
    setState(LicenseState::Error, result.error);
    return result;
  }

  if (!*ok) {
    const auto error = stringField(*json, "error").value_or("INVALID_RESPONSE");
    result.error = apiErrorCode(error);
    result.errorMessage = error;
    setState(stateForError(result.error), result.error);
    return result;
  }

  std::string removeMessage;
  if (!removeCache(removeMessage)) {
    result.error = ErrorCode::CacheIoError;
    result.errorMessage = removeMessage;
    setState(LicenseState::Error, result.error);
    return result;
  }

  result.ok = true;
  result.seatsUsed = intField(*json, "seatsUsed").value_or(0);
  cachedLicense_.reset();
  setState(LicenseState::Unlicensed);
  return result;
}

LicenseState Client::state() const {
  return state_;
}

ErrorCode Client::lastError() const {
  return lastError_;
}

std::optional<LicenseInfo> Client::cachedLicense() const {
  return cachedLicense_;
}

std::string Client::machineIdForProduct(const std::string& productId) const {
  if (!config_.machineId.empty()) {
    return config_.machineId;
  }

  const std::string fingerprint =
    config_.machineFingerprint.empty() ? defaultMachineFingerprint() : config_.machineFingerprint;
  return deriveMachineId(productId, fingerprint);
}

std::chrono::system_clock::time_point Client::now() const {
  return config_.clock ? config_.clock() : std::chrono::system_clock::now();
}

std::string Client::endpointUrl(const std::string& action) const {
  return trimTrailingSlash(config_.baseUrl) + "/" + action;
}

void Client::loadKeysetCache() {
  const auto cache = readKeysetCacheFile(keysetCachePathFor(config_.cachePath));
  if (!cache) {
    return;
  }

  mergeKeyset(config_.keyset, cache->keys, false);
  keysetFetchedAt_ = cache->fetchedAt;
}

void Client::refreshKeyset() {
  try {
    if (!config_.http) {
      return;
    }

    const std::string url = config_.keysUrl.empty()
      ? (config_.baseUrl.empty() ? std::string{} : endpointUrl("keys"))
      : config_.keysUrl;
    if (url.empty()) {
      return;
    }

    const HttpResponse response = config_.http->getJson(url, {
      {"Accept", "application/json"},
    });

    if (response.statusCode != 200) {
      return;
    }

    auto keys = parseRemoteKeyset(response.body);
    if (!keys) {
      return;
    }

    const auto fetchedAt = now();
    mergeKeyset(config_.keyset, *keys, true);
    keysetFetchedAt_ = fetchedAt;

    if (!config_.cachePath.empty()) {
      (void)writeKeysetCacheFile(
        keysetCachePathFor(config_.cachePath),
        KeysetCacheRecord{*keys, fetchedAt}
      );
    }
  } catch (...) {
  }
}

void Client::refreshKeysetIfDue() {
  loadKeysetCache();

  if (!keysetFetchedAt_) {
    refreshKeyset();
    return;
  }

  if (config_.keysetTtl <= std::chrono::seconds::zero()) {
    refreshKeyset();
    return;
  }

  if (now() - *keysetFetchedAt_ >= config_.keysetTtl) {
    refreshKeyset();
  }
}

VerifyTokenResult Client::verifyTokenWithKeysetRefresh(
  const std::string& token,
  const std::string& expectedProductId,
  const std::string& expectedMachineId,
  std::chrono::system_clock::time_point currentTime
) {
  loadKeysetCache();

  auto verified = verifyLicenseToken(
    token,
    config_.publicKeyPem,
    expectedProductId,
    expectedMachineId,
    currentTime,
    config_.keyset
  );

  if (!isMissingKidFailure(verified)) {
    return verified;
  }

  refreshKeyset();
  return verifyLicenseToken(
    token,
    config_.publicKeyPem,
    expectedProductId,
    expectedMachineId,
    currentTime,
    config_.keyset
  );
}

std::optional<Client::CacheRecord> Client::readCache(ErrorCode& error, std::string& message) const {
  error = ErrorCode::None;
  message.clear();

  if (config_.cachePath.empty()) {
    error = ErrorCode::ConfigError;
    message = "cachePath is required.";
    return std::nullopt;
  }

  std::ifstream input(config_.cachePath);
  if (!input) {
    error = ErrorCode::CacheMissing;
    message = "License cache does not exist.";
    return std::nullopt;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  const auto json = parseJsonObject(buffer.str());

  if (!json) {
    error = ErrorCode::InvalidCache;
    message = "License cache is not valid JSON.";
    return std::nullopt;
  }

  auto productId = stringField(*json, "productId");
  auto licenseKey = stringField(*json, "licenseKey");
  auto machineId = stringField(*json, "machineId");
  auto licenseToken = stringField(*json, "license");

  if (!productId || !licenseKey || !machineId || !licenseToken) {
    error = ErrorCode::InvalidCache;
    message = "License cache is missing a required field.";
    return std::nullopt;
  }

  return CacheRecord{*productId, *licenseKey, *machineId, *licenseToken};
}

bool Client::writeCache(const CacheRecord& cache, std::string& message) const {
  message.clear();

  if (config_.cachePath.empty()) {
    message = "cachePath is required.";
    return false;
  }

  try {
    const auto parent = config_.cachePath.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }

    const auto body = makeJsonObject({
      {"version", "1"},
      {"productId", quoteJson(cache.productId)},
      {"licenseKey", quoteJson(cache.licenseKey)},
      {"machineId", quoteJson(cache.machineId)},
      {"license", quoteJson(cache.licenseToken)},
      {"savedAt", quoteJson(formatIso8601(now()))},
    });
    if (!writeFileAtomically(config_.cachePath, body)) {
      message = "Could not atomically write license cache.";
      return false;
    }
    return true;
  } catch (const std::exception& exception) {
    message = exception.what();
    return false;
  }
}

bool Client::removeCache(std::string& message) const {
  message.clear();

  try {
    if (config_.cachePath.empty() || !std::filesystem::exists(config_.cachePath)) {
      return true;
    }

    std::filesystem::remove(config_.cachePath);
    return true;
  } catch (const std::exception& exception) {
    message = exception.what();
    return false;
  }
}

bool Client::tryOnlineVerify(const CacheRecord& cache, LicenseInfo& license) {
  if (config_.baseUrl.empty() || !config_.http) {
    setState(LicenseState::NetworkUnavailable, ErrorCode::NetworkError);
    return false;
  }

  const std::string body = makeJsonObject({
    {"productKey", quoteJson(cache.productId)},
    {"licenseKey", quoteJson(cache.licenseKey)},
    {"machineId", quoteJson(cache.machineId)},
  });

  HttpResponse response;
  try {
    response = config_.http->postJson(endpointUrl("verify"), body, {
      {"Content-Type", "application/json"},
      {"Accept", "application/json"},
    });
  } catch (...) {
    setState(LicenseState::NetworkUnavailable, ErrorCode::NetworkError);
    return false;
  }

  if (!httpOk(response.statusCode)) {
    const auto failure = classifyHttpFailure(response, "Verify");
    setState(failure.state, failure.error);
    return false;
  }

  const auto json = parseJsonObject(response.body);
  if (!json) {
    setState(LicenseState::Error, ErrorCode::InvalidResponse);
    return false;
  }

  const auto ok = boolField(*json, "ok");
  if (!ok) {
    setState(LicenseState::Error, ErrorCode::InvalidResponse);
    return false;
  }

  if (!*ok) {
    const auto error = apiErrorCode(stringField(*json, "error").value_or("INVALID_RESPONSE"));
    setState(stateForError(error), error);
    return false;
  }

  const auto token = stringField(*json, "license");
  if (!token) {
    setState(LicenseState::Error, ErrorCode::InvalidResponse);
    return false;
  }

  auto verified = verifyTokenWithKeysetRefresh(
    *token,
    cache.productId,
    cache.machineId,
    now()
  );
  if (!verified.ok || !verified.license) {
    setState(stateForError(verified.error), verified.error);
    return false;
  }

  CacheRecord nextCache = cache;
  nextCache.licenseToken = *token;
  std::string writeMessage;
  if (!writeCache(nextCache, writeMessage)) {
    setState(LicenseState::Error, ErrorCode::CacheIoError);
    return false;
  }

  license = *verified.license;
  refreshKeysetIfDue();
  return true;
}

void Client::setState(LicenseState state, ErrorCode error) {
  state_ = state;
  lastError_ = error;
}

} // namespace otomarket::license
