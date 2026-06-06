#include <otomarket/license/LicenseSdk.h>

#include <algorithm>
#include <array>
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
#include <unistd.h>
#endif

extern "C" {
#include "tweetnacl.h"
}

namespace otomarket::license {
namespace {

constexpr std::array<unsigned char, 12> kEd25519SpkiPrefix = {
  0x30, 0x2a, 0x30, 0x05, 0x06, 0x03,
  0x2b, 0x65, 0x70, 0x03, 0x21, 0x00
};

struct JsonValue {
  enum class Type {
    Null,
    Bool,
    Number,
    String,
    Object,
  };

  Type type = Type::Null;
  bool boolValue = false;
  double numberValue = 0;
  std::string stringValue;
  std::map<std::string, JsonValue> objectValue;
};

class JsonParser {
public:
  explicit JsonParser(const std::string& source)
    : source_(source) {}

  JsonValue parse() {
    skipWhitespace();
    JsonValue value = parseValue();
    skipWhitespace();

    if (position_ != source_.size()) {
      throw std::runtime_error("Unexpected trailing JSON input.");
    }

    return value;
  }

private:
  JsonValue parseValue() {
    skipWhitespace();

    if (position_ >= source_.size()) {
      throw std::runtime_error("Unexpected end of JSON input.");
    }

    const char ch = source_[position_];

    if (ch == '{') {
      return parseObject();
    }

    if (ch == '"') {
      JsonValue value;
      value.type = JsonValue::Type::String;
      value.stringValue = parseString();
      return value;
    }

    if (ch == 't') {
      consumeLiteral("true");
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.boolValue = true;
      return value;
    }

    if (ch == 'f') {
      consumeLiteral("false");
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.boolValue = false;
      return value;
    }

    if (ch == 'n') {
      consumeLiteral("null");
      return {};
    }

    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      return parseNumber();
    }

    throw std::runtime_error("Unsupported JSON value.");
  }

  JsonValue parseObject() {
    JsonValue value;
    value.type = JsonValue::Type::Object;
    expect('{');
    skipWhitespace();

    if (peek('}')) {
      ++position_;
      return value;
    }

    while (true) {
      skipWhitespace();
      const std::string key = parseString();
      skipWhitespace();
      expect(':');
      value.objectValue.emplace(key, parseValue());
      skipWhitespace();

      if (peek('}')) {
        ++position_;
        return value;
      }

      expect(',');
    }
  }

  JsonValue parseNumber() {
    const size_t start = position_;

    if (peek('-')) {
      ++position_;
    }

    while (position_ < source_.size() &&
           std::isdigit(static_cast<unsigned char>(source_[position_])) != 0) {
      ++position_;
    }

    if (peek('.')) {
      ++position_;
      while (position_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[position_])) != 0) {
        ++position_;
      }
    }

    if (peek('e') || peek('E')) {
      ++position_;
      if (peek('+') || peek('-')) {
        ++position_;
      }
      while (position_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[position_])) != 0) {
        ++position_;
      }
    }

    JsonValue value;
    value.type = JsonValue::Type::Number;
    value.numberValue = std::stod(source_.substr(start, position_ - start));
    return value;
  }

  std::string parseString() {
    expect('"');
    std::string result;

    while (position_ < source_.size()) {
      const char ch = source_[position_++];

      if (ch == '"') {
        return result;
      }

      if (ch != '\\') {
        result.push_back(ch);
        continue;
      }

      if (position_ >= source_.size()) {
        throw std::runtime_error("Unexpected end of JSON escape.");
      }

      const char escaped = source_[position_++];

      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          result.push_back(escaped);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u':
          appendUnicodeEscape(result);
          break;
        default:
          throw std::runtime_error("Unsupported JSON escape.");
      }
    }

    throw std::runtime_error("Unterminated JSON string.");
  }

  void appendUnicodeEscape(std::string& output) {
    if (position_ + 4 > source_.size()) {
      throw std::runtime_error("Invalid JSON unicode escape.");
    }

    unsigned int codepoint = 0;
    for (int index = 0; index < 4; ++index) {
      const char ch = source_[position_++];
      codepoint <<= 4;

      if (ch >= '0' && ch <= '9') {
        codepoint += static_cast<unsigned int>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        codepoint += static_cast<unsigned int>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        codepoint += static_cast<unsigned int>(ch - 'A' + 10);
      } else {
        throw std::runtime_error("Invalid JSON unicode escape.");
      }
    }

    if (codepoint <= 0x7f) {
      output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  void consumeLiteral(const char* literal) {
    const size_t length = std::strlen(literal);

    if (source_.compare(position_, length, literal) != 0) {
      throw std::runtime_error("Invalid JSON literal.");
    }

    position_ += length;
  }

  void expect(char expected) {
    if (position_ >= source_.size() || source_[position_] != expected) {
      throw std::runtime_error("Unexpected JSON character.");
    }
    ++position_;
  }

  bool peek(char expected) const {
    return position_ < source_.size() && source_[position_] == expected;
  }

  void skipWhitespace() {
    while (position_ < source_.size() &&
           std::isspace(static_cast<unsigned char>(source_[position_])) != 0) {
      ++position_;
    }
  }

  const std::string& source_;
  size_t position_ = 0;
};

std::string trim(const std::string& value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }

  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return value.substr(start, end - start);
}

std::string replaceEscapedNewlines(std::string value) {
  size_t position = 0;
  while ((position = value.find("\\n", position)) != std::string::npos) {
    value.replace(position, 2, "\n");
    ++position;
  }
  return value;
}

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

const JsonValue* objectField(const JsonValue& value, const std::string& key) {
  if (value.type != JsonValue::Type::Object) {
    return nullptr;
  }

  const auto found = value.objectValue.find(key);
  if (found == value.objectValue.end()) {
    return nullptr;
  }

  return &found->second;
}

std::optional<std::string> stringField(const JsonValue& value, const std::string& key) {
  const JsonValue* field = objectField(value, key);
  if (field == nullptr || field->type != JsonValue::Type::String) {
    return std::nullopt;
  }
  return field->stringValue;
}

std::optional<bool> boolField(const JsonValue& value, const std::string& key) {
  const JsonValue* field = objectField(value, key);
  if (field == nullptr || field->type != JsonValue::Type::Bool) {
    return std::nullopt;
  }
  return field->boolValue;
}

std::optional<int> intField(const JsonValue& value, const std::string& key) {
  const JsonValue* field = objectField(value, key);
  if (field == nullptr || field->type != JsonValue::Type::Number) {
    return std::nullopt;
  }
  return static_cast<int>(field->numberValue);
}

bool isNullField(const JsonValue& value, const std::string& key) {
  const JsonValue* field = objectField(value, key);
  return field != nullptr && field->type == JsonValue::Type::Null;
}

std::vector<std::string> splitToken(const std::string& token) {
  std::vector<std::string> parts;
  size_t start = 0;

  while (true) {
    const size_t dot = token.find('.', start);

    if (dot == std::string::npos) {
      parts.push_back(token.substr(start));
      break;
    }

    parts.push_back(token.substr(start, dot - start));
    start = dot + 1;
  }

  return parts;
}

int base64Value(char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return ch - 'A';
  }
  if (ch >= 'a' && ch <= 'z') {
    return ch - 'a' + 26;
  }
  if (ch >= '0' && ch <= '9') {
    return ch - '0' + 52;
  }
  if (ch == '+' || ch == '-') {
    return 62;
  }
  if (ch == '/' || ch == '_') {
    return 63;
  }
  return -1;
}

std::vector<unsigned char> base64Decode(const std::string& input) {
  std::vector<unsigned char> output;
  int value = 0;
  int bits = -8;

  for (const char ch : input) {
    if (ch == '=') {
      break;
    }

    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }

    const int decoded = base64Value(ch);
    if (decoded < 0) {
      throw std::runtime_error("Invalid base64 input.");
    }

    value = (value << 6) + decoded;
    bits += 6;

    if (bits >= 0) {
      output.push_back(static_cast<unsigned char>((value >> bits) & 0xff));
      bits -= 8;
    }
  }

  return output;
}

bool isHexPublicKey(const std::string& value) {
  if (value.size() != 64) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
  });
}

std::array<unsigned char, 32> parseHexPublicKey(const std::string& value) {
  std::array<unsigned char, 32> key{};

  for (size_t index = 0; index < key.size(); ++index) {
    const std::string byte = value.substr(index * 2, 2);
    key[index] = static_cast<unsigned char>(std::stoul(byte, nullptr, 16));
  }

  return key;
}

std::array<unsigned char, 32> parsePublicKey(const std::string& publicKeyPem) {
  std::string normalized = trim(replaceEscapedNewlines(publicKeyPem));

  if (normalized.empty()) {
    throw std::runtime_error("Public key is empty.");
  }

  if (isHexPublicKey(normalized)) {
    return parseHexPublicKey(normalized);
  }

  std::string body;
  std::istringstream input(normalized);
  std::string line;

  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() ||
        line.find("-----BEGIN") == 0 ||
        line.find("-----END") == 0) {
      continue;
    }
    body += line;
  }

  if (body.empty()) {
    body = normalized;
  }

  const std::vector<unsigned char> der = base64Decode(body);

  if (der.size() == 32) {
    std::array<unsigned char, 32> raw{};
    std::copy(der.begin(), der.end(), raw.begin());
    return raw;
  }

  if (der.size() == kEd25519SpkiPrefix.size() + 32 &&
      std::equal(kEd25519SpkiPrefix.begin(), kEd25519SpkiPrefix.end(), der.begin())) {
    std::array<unsigned char, 32> raw{};
    std::copy(der.end() - 32, der.end(), raw.begin());
    return raw;
  }

  throw std::runtime_error("Public key is not an Ed25519 SPKI PEM or raw key.");
}

bool verifyEd25519Detached(
  const std::string& message,
  const std::vector<unsigned char>& signature,
  const std::array<unsigned char, 32>& publicKey
) {
  if (signature.size() != crypto_sign_BYTES) {
    return false;
  }

  std::vector<unsigned char> signedMessage(signature.size() + message.size());
  std::copy(signature.begin(), signature.end(), signedMessage.begin());
  std::copy(message.begin(), message.end(), signedMessage.begin() + signature.size());

  std::vector<unsigned char> opened(signedMessage.size());
  unsigned long long openedLength = 0;

  const int result = crypto_sign_open(
    opened.data(),
    &openedLength,
    signedMessage.data(),
    static_cast<unsigned long long>(signedMessage.size()),
    publicKey.data()
  );

  if (result != 0 || openedLength != message.size()) {
    return false;
  }

  return std::equal(
    opened.begin(),
    opened.begin() + static_cast<std::ptrdiff_t>(openedLength),
    message.begin()
  );
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

std::time_t timegmPortable(std::tm* time) {
#ifdef _WIN32
  return _mkgmtime(time);
#else
  return timegm(time);
#endif
}

std::tm gmtimePortable(std::time_t value) {
  std::tm result{};
#ifdef _WIN32
  gmtime_s(&result, &value);
#else
  gmtime_r(&value, &result);
#endif
  return result;
}

int parseDigits(const std::string& value, size_t position, size_t length) {
  if (position + length > value.size()) {
    throw std::runtime_error("Invalid ISO date.");
  }

  int parsed = 0;
  for (size_t index = 0; index < length; ++index) {
    const char ch = value[position + index];
    if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
      throw std::runtime_error("Invalid ISO date.");
    }
    parsed = parsed * 10 + (ch - '0');
  }

  return parsed;
}

std::chrono::system_clock::time_point parseIso8601(const std::string& value) {
  if (value.size() < 20 ||
      value[4] != '-' ||
      value[7] != '-' ||
      (value[10] != 'T' && value[10] != 't') ||
      value[13] != ':' ||
      value[16] != ':') {
    throw std::runtime_error("Invalid ISO date.");
  }

  std::tm time{};
  time.tm_year = parseDigits(value, 0, 4) - 1900;
  time.tm_mon = parseDigits(value, 5, 2) - 1;
  time.tm_mday = parseDigits(value, 8, 2);
  time.tm_hour = parseDigits(value, 11, 2);
  time.tm_min = parseDigits(value, 14, 2);
  time.tm_sec = parseDigits(value, 17, 2);

  size_t position = 19;
  int milliseconds = 0;

  if (position < value.size() && value[position] == '.') {
    ++position;
    int scale = 100;
    while (position < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[position])) != 0) {
      if (scale > 0) {
        milliseconds += (value[position] - '0') * scale;
        scale /= 10;
      }
      ++position;
    }
  }

  if (position >= value.size() || value[position] != 'Z') {
    throw std::runtime_error("Only UTC ISO dates are supported.");
  }

  const std::time_t seconds = timegmPortable(&time);
  if (seconds == static_cast<std::time_t>(-1)) {
    throw std::runtime_error("Invalid ISO date.");
  }

  return std::chrono::system_clock::from_time_t(seconds) +
         std::chrono::milliseconds(milliseconds);
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

ErrorCode apiErrorCode(const std::string& error) {
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
  return ErrorCode::InvalidResponse;
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
    case ErrorCode::NetworkError:
      return LicenseState::NetworkUnavailable;
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
  std::optional<std::chrono::system_clock::time_point> now
) {
  VerifyTokenResult result;
  const std::vector<std::string> parts = splitToken(token);

  if (parts.size() != 3 || parts[0].empty() || parts[1].empty() || parts[2].empty()) {
    result.error = ErrorCode::InvalidSignature;
    result.errorMessage = "License token must use header.payload.signature format.";
    return result;
  }

  JsonValue header;
  std::string payloadJson;
  std::vector<unsigned char> signature;
  std::array<unsigned char, 32> publicKey{};

  try {
    const std::vector<unsigned char> headerBytes = base64Decode(parts[0]);
    const std::vector<unsigned char> payloadBytes = base64Decode(parts[1]);
    signature = base64Decode(parts[2]);
    header = JsonParser(std::string(headerBytes.begin(), headerBytes.end())).parse();
    payloadJson = std::string(payloadBytes.begin(), payloadBytes.end());
    publicKey = parsePublicKey(publicKeyPem);
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
    result.error = ErrorCode::NetworkError;
    result.errorMessage = "Activation request failed with HTTP " + std::to_string(response.statusCode) + ".";
    setState(LicenseState::NetworkUnavailable, result.error);
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

  auto verified = verifyLicenseToken(*licenseToken, config_.publicKeyPem, productId, machineId, now());
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

  auto verified = verifyLicenseToken(
    cache->licenseToken,
    config_.publicKeyPem,
    expectedProductId,
    cache->machineId,
    now()
  );

  if (!verified.ok || !verified.license) {
    setState(stateForError(verified.error), verified.error);
    cachedLicense_.reset();
    return false;
  }

  cachedLicense_ = verified.license;
  const auto currentTime = now();

  if (verified.license->expiresAt && *verified.license->expiresAt <= currentTime) {
    setState(LicenseState::Expired, ErrorCode::Expired);
    return false;
  }

  if (currentTime < verified.license->verifyAfter) {
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
    const auto graceDeadline = verified.license->verifyAfter + config_.verifyRetryGrace;

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
    result.error = ErrorCode::NetworkError;
    result.errorMessage = "Deactivate request failed with HTTP " + std::to_string(response.statusCode) + ".";
    setState(LicenseState::NetworkUnavailable, result.error);
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

    auto temporaryPath = config_.cachePath;
    temporaryPath += ".tmp";

    {
      std::ofstream output(temporaryPath, std::ios::trunc);
      if (!output) {
        message = "Could not open temporary cache file for writing.";
        return false;
      }

      output << makeJsonObject({
        {"version", "1"},
        {"productId", quoteJson(cache.productId)},
        {"licenseKey", quoteJson(cache.licenseKey)},
        {"machineId", quoteJson(cache.machineId)},
        {"license", quoteJson(cache.licenseToken)},
        {"savedAt", quoteJson(formatIso8601(now()))},
      });

      if (!output) {
        message = "Could not write license cache.";
        return false;
      }
    }

    if (std::filesystem::exists(config_.cachePath)) {
      std::filesystem::remove(config_.cachePath);
    }

    std::filesystem::rename(temporaryPath, config_.cachePath);
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
    setState(LicenseState::NetworkUnavailable, ErrorCode::NetworkError);
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

  auto verified = verifyLicenseToken(*token, config_.publicKeyPem, cache.productId, cache.machineId, now());
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
  return true;
}

void Client::setState(LicenseState state, ErrorCode error) {
  state_ = state;
  lastError_ = error;
}

} // namespace otomarket::license
