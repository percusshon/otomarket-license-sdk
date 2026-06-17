#include <otomarket/license/LicenseSdk.h>
#include <otomarket/license/LicenseActivationUi.h>
#include <otomarket/license/LicenseSnapshot.h>
#include <otomarket/license/Version.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include "tweetnacl.h"
}

using otomarket::license::ActivateResult;
using otomarket::license::Client;
using otomarket::license::Config;
using otomarket::license::ErrorCode;
using otomarket::license::HttpHeader;
using otomarket::license::HttpResponse;
using otomarket::license::HttpTransport;
using otomarket::license::LicenseState;
using otomarket::license::PackContentResult;
using otomarket::license::SnapshotSource;
using otomarket::license::SubscriptionStatus;

namespace {

constexpr const char* kNowIso = "2026-06-04T12:00:00.000Z";
constexpr const char* kPastIso = "2026-06-04T11:00:00.000Z";
constexpr const char* kOlderPastIso = "2026-06-04T09:00:00.000Z";
constexpr const char* kFutureIso = "2026-06-18T12:00:00.000Z";
constexpr const char* kPackBuyoutContentHash =
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* kPackTrialContentHash =
  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

struct TestFailure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct KeyPair {
  std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> publicKey{};
  std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey{};
  std::string publicKeyPem;
};

struct HttpRequest {
  std::string url;
  std::string body;
  std::vector<HttpHeader> headers;
};

struct LocalizedActivationStringsCase {
  std::string locale;
  otomarket::license::LicenseActivationStrings strings;
  std::string licenseKeyLabel;
};

class MockHttpTransport final : public HttpTransport {
public:
  std::vector<HttpRequest> requests;
  std::vector<HttpRequest> getRequests;
  std::function<HttpResponse(const HttpRequest&)> handler;
  std::function<HttpResponse(const HttpRequest&)> getHandler;

  HttpResponse postJson(
    const std::string& url,
    const std::string& body,
    const std::vector<HttpHeader>& headers
  ) override {
    requests.push_back({url, body, headers});
    if (handler) {
      return handler(requests.back());
    }
    return {500, "{}"};
  }

  HttpResponse getJson(
    const std::string& url,
    const std::vector<HttpHeader>& headers
  ) override {
    getRequests.push_back({url, {}, headers});
    if (getHandler) {
      return getHandler(getRequests.back());
    }
    return {};
  }
};

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw TestFailure(message);
  }
}

template <typename T, typename U>
void requireEqual(const T& actual, const U& expected, const std::string& message) {
  if (!(actual == expected)) {
    std::ostringstream output;
    output << message;
    throw TestFailure(output.str());
  }
}

std::array<std::pair<std::string, std::string>, 41> activationStringFields(
  const otomarket::license::LicenseActivationStrings& strings
) {
  return {{
    {"licenseKeyLabel", strings.licenseKeyLabel},
    {"licenseKeyPlaceholder", strings.licenseKeyPlaceholder},
    {"activateButton", strings.activateButton},
    {"deactivateButton", strings.deactivateButton},
    {"trialButton", strings.trialButton},
    {"checkingStatus", strings.checkingStatus},
    {"activatingStatus", strings.activatingStatus},
    {"deactivatingStatus", strings.deactivatingStatus},
    {"unknownStatus", strings.unknownStatus},
    {"unlicensedStatus", strings.unlicensedStatus},
    {"licensedStatus", strings.licensedStatus},
    {"offlineGraceStatus", strings.offlineGraceStatus},
    {"verifyDueStatus", strings.verifyDueStatus},
    {"expiredStatus", strings.expiredStatus},
    {"revokedStatus", strings.revokedStatus},
    {"notFoundStatus", strings.notFoundStatus},
    {"productMismatchStatus", strings.productMismatchStatus},
    {"seatLimitStatus", strings.seatLimitStatus},
    {"invalidCacheStatus", strings.invalidCacheStatus},
    {"networkUnavailableStatus", strings.networkUnavailableStatus},
    {"errorStatus", strings.errorStatus},
    {"seatsLabel", strings.seatsLabel},
    {"maxActivationsLabel", strings.maxActivationsLabel},
    {"expiresAtLabel", strings.expiresAtLabel},
    {"noExpirationText", strings.noExpirationText},
    {"offlineGraceDetail", strings.offlineGraceDetail},
    {"errorSeatLimit", strings.errorSeatLimit},
    {"errorExpired", strings.errorExpired},
    {"errorRevoked", strings.errorRevoked},
    {"errorNotFound", strings.errorNotFound},
    {"errorProductMismatch", strings.errorProductMismatch},
    {"errorNetwork", strings.errorNetwork},
    {"errorRequestRejected", strings.errorRequestRejected},
    {"errorInvalidResponse", strings.errorInvalidResponse},
    {"errorInvalidSignature", strings.errorInvalidSignature},
    {"errorInvalidCache", strings.errorInvalidCache},
    {"errorCacheIo", strings.errorCacheIo},
    {"errorCacheMissing", strings.errorCacheMissing},
    {"errorMachineMismatch", strings.errorMachineMismatch},
    {"errorConfig", strings.errorConfig},
    {"errorFallback", strings.errorFallback},
  }};
}

std::chrono::system_clock::time_point makeTime(
  int year,
  int month,
  int day,
  int hour,
  int minute,
  int second
) {
  std::tm time{};
  time.tm_year = year - 1900;
  time.tm_mon = month - 1;
  time.tm_mday = day;
  time.tm_hour = hour;
  time.tm_min = minute;
  time.tm_sec = second;

#ifdef _WIN32
  const std::time_t epoch = _mkgmtime(&time);
#else
  const std::time_t epoch = timegm(&time);
#endif

  return std::chrono::system_clock::from_time_t(epoch);
}

std::chrono::system_clock::time_point now() {
  return makeTime(2026, 6, 4, 12, 0, 0);
}

std::string base64Encode(const std::vector<unsigned char>& bytes) {
  static constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  int value = 0;
  int bits = -6;

  for (const unsigned char byte : bytes) {
    value = (value << 8) + byte;
    bits += 8;
    while (bits >= 0) {
      output.push_back(kAlphabet[(value >> bits) & 0x3f]);
      bits -= 6;
    }
  }

  if (bits > -6) {
    output.push_back(kAlphabet[((value << 8) >> (bits + 8)) & 0x3f]);
  }

  while (output.size() % 4 != 0) {
    output.push_back('=');
  }

  return output;
}

std::string base64UrlEncode(const std::vector<unsigned char>& bytes) {
  std::string output = base64Encode(bytes);
  std::replace(output.begin(), output.end(), '+', '-');
  std::replace(output.begin(), output.end(), '/', '_');
  while (!output.empty() && output.back() == '=') {
    output.pop_back();
  }
  return output;
}

std::string base64UrlEncode(const std::string& value) {
  return base64UrlEncode(std::vector<unsigned char>(value.begin(), value.end()));
}

std::string jsonString(const std::string& value) {
  std::string output = "\"";
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') {
      output.push_back('\\');
    }
    output.push_back(ch);
  }
  output.push_back('"');
  return output;
}

std::string publicPemFromRawKey(const std::array<unsigned char, crypto_sign_PUBLICKEYBYTES>& publicKey) {
  const std::array<unsigned char, 12> prefix = {
    0x30, 0x2a, 0x30, 0x05, 0x06, 0x03,
    0x2b, 0x65, 0x70, 0x03, 0x21, 0x00
  };
  std::vector<unsigned char> der(prefix.begin(), prefix.end());
  der.insert(der.end(), publicKey.begin(), publicKey.end());
  const std::string encoded = base64Encode(der);

  std::ostringstream pem;
  pem << "-----BEGIN PUBLIC KEY-----\n";
  for (size_t position = 0; position < encoded.size(); position += 64) {
    pem << encoded.substr(position, 64) << "\n";
  }
  pem << "-----END PUBLIC KEY-----\n";
  return pem.str();
}

std::string escapedNewlinePem(const std::string& pem) {
  std::string output;
  output.reserve(pem.size());

  for (const char ch : pem) {
    if (ch == '\n') {
      output += "\\n";
    } else {
      output.push_back(ch);
    }
  }

  return output;
}

std::string compactPem(const std::string& pem) {
  std::string output;
  output.reserve(pem.size());

  for (const char ch : pem) {
    if (ch != '\n' && ch != '\r') {
      output.push_back(ch);
    }
  }

  return output;
}

std::string publicKeyBase64BodyFromPem(const std::string& pem) {
  std::istringstream input(pem);
  std::string line;
  std::string body;

  while (std::getline(input, line)) {
    if (line.find("-----BEGIN") == 0 || line.find("-----END") == 0) {
      continue;
    }
    body += line;
  }

  return body;
}

std::string publicKeyHexFromRawKey(const std::array<unsigned char, crypto_sign_PUBLICKEYBYTES>& publicKey) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');

  for (const unsigned char byte : publicKey) {
    output << std::setw(2) << static_cast<int>(byte);
  }

  return output.str();
}

KeyPair generateKeyPair() {
  KeyPair keyPair;
  crypto_sign_keypair(keyPair.publicKey.data(), keyPair.secretKey.data());
  keyPair.publicKeyPem = publicPemFromRawKey(keyPair.publicKey);
  return keyPair;
}

std::string payloadJson(
  const std::string& productId,
  const std::string& machineId,
  const std::string& verifyAfter,
  const std::string& expiresAt = "null",
  const std::string& issuedAt = kNowIso,
  const std::string& extraFields = ""
) {
  return std::string("{") +
    "\"licenseId\":\"license-1\"," +
    "\"productId\":" + jsonString(productId) + "," +
    "\"buyerId\":\"buyer-1\"," +
    "\"machineId\":" + jsonString(machineId) + "," +
    "\"maxActivations\":2," +
    "\"issuedAt\":" + jsonString(issuedAt) + "," +
    "\"expiresAt\":" + expiresAt + "," +
    "\"verifyAfter\":" + jsonString(verifyAfter) +
    extraFields +
    "}";
}

std::string headerJsonWithKid(const std::string& kid) {
  return std::string("{\"alg\":\"EdDSA\",\"typ\":\"JWT\",\"kid\":") + jsonString(kid) + "}";
}

std::string signToken(
  const KeyPair& keyPair,
  const std::string& payload,
  const std::string& header = R"({"alg":"EdDSA","typ":"JWT"})"
) {
  const std::string signingInput = base64UrlEncode(header) + "." + base64UrlEncode(payload);
  std::vector<unsigned char> signedMessage(crypto_sign_BYTES + signingInput.size());
  unsigned long long signedLength = 0;

  crypto_sign(
    signedMessage.data(),
    &signedLength,
    reinterpret_cast<const unsigned char*>(signingInput.data()),
    static_cast<unsigned long long>(signingInput.size()),
    keyPair.secretKey.data()
  );

  signedMessage.resize(crypto_sign_BYTES);
  return signingInput + "." + base64UrlEncode(signedMessage);
}

std::string licenseToken(
  const KeyPair& keyPair,
  const std::string& productId = "product-1",
  const std::string& machineId = "machine-1",
  const std::string& verifyAfter = kFutureIso,
  const std::string& expiresAt = "null",
  const std::string& issuedAt = kNowIso,
  const std::string& kid = "",
  const std::string& extraFields = ""
) {
  const std::string payload = payloadJson(
    productId,
    machineId,
    verifyAfter,
    expiresAt,
    issuedAt,
    extraFields
  );
  if (!kid.empty()) {
    return signToken(keyPair, payload, headerJsonWithKid(kid));
  }
  return signToken(keyPair, payload);
}

long long epochSeconds(std::chrono::system_clock::time_point value) {
  return static_cast<long long>(std::chrono::system_clock::to_time_t(value));
}

std::string snapshotPayloadJson(
  const std::string& schemaVersion = "2",
  bool includePacks = false
) {
  const auto snapshotIat = makeTime(2026, 6, 4, 12, 0, 0);
  const auto snapshotExp = makeTime(2026, 6, 4, 12, 5, 0);
  const auto offlineGraceExpiresAt = makeTime(2026, 6, 11, 12, 0, 0);
  const std::string buyoutPacks = includePacks
    ? std::string(",\"packs\":[") +
        "{\"packId\":\"pack-buyout\",\"contentHash\":\"" + kPackBuyoutContentHash + "\"}," +
        "{\"packId\":\"pack-bundle-b\",\"contentHash\":null}" +
      "]"
    : "";
  const std::string activeTrialPacks = includePacks
    ? std::string(",\"packs\":[") +
        "{\"packId\":\"pack-trial-active\",\"contentHash\":\"" + kPackTrialContentHash + "\"}" +
      "]"
    : "";

  return std::string("{") +
    "\"schemaVersion\":" + schemaVersion + "," +
    "\"issuer\":\"OtoMarket\"," +
    "\"sub\":\"user-1\"," +
    "\"iat\":" + std::to_string(epochSeconds(snapshotIat)) + "," +
    "\"exp\":" + std::to_string(epochSeconds(snapshotExp)) + "," +
    "\"offlineGracePeriodDays\":7," +
    "\"offlineGraceExpiresAt\":" + std::to_string(epochSeconds(offlineGraceExpiresAt)) + "," +
    "\"entitlements\":[" +
      "{" +
        "\"productId\":\"product-buyout\"," +
        "\"packId\":\"pack-buyout\"," +
        "\"packIds\":[\"pack-buyout\",\"pack-bundle-b\"]," +
        "\"saleMode\":\"BUYOUT\"," +
        "\"source\":\"buyout\"," +
        "\"expiresAt\":null," +
        "\"subscriptionStatus\":null," +
        "\"status\":\"active\"," +
        "\"licenseKey\":\"OTMK-BUYOUT\"," +
        "\"licenseKeys\":[\"OTMK-BUYOUT\"]," +
        "\"acquiredAt\":\"2026-03-01T00:00:00.000Z\"" +
        buyoutPacks +
      "}," +
      "{" +
        "\"productId\":\"product-free\"," +
        "\"packId\":\"pack-free\"," +
        "\"packIds\":[\"pack-free\"]," +
        "\"saleMode\":\"FREE\"," +
        "\"source\":\"free\"," +
        "\"expiresAt\":null," +
        "\"subscriptionStatus\":null," +
        "\"status\":\"active\"," +
        "\"licenseKey\":null," +
        "\"licenseKeys\":[]," +
        "\"acquiredAt\":\"2026-03-02T00:00:00.000Z\"" +
      "}," +
      "{" +
        "\"productId\":\"product-sub-active\"," +
        "\"packId\":\"pack-sub-active\"," +
        "\"packIds\":[\"pack-sub-active\"]," +
        "\"saleMode\":\"SUBSCRIPTION\"," +
        "\"source\":\"subscription\"," +
        "\"expiresAt\":\"2026-06-18T12:00:00.000Z\"," +
        "\"subscriptionStatus\":\"active\"," +
        "\"status\":\"active\"," +
        "\"licenseKey\":\"OTMK-SUB-ACTIVE\"," +
        "\"licenseKeys\":[\"OTMK-SUB-ACTIVE\"]," +
        "\"acquiredAt\":\"2026-03-03T00:00:00.000Z\"" +
      "}," +
      "{" +
        "\"productId\":\"product-sub-past-due\"," +
        "\"packId\":\"pack-sub-past-due\"," +
        "\"packIds\":[\"pack-sub-past-due\"]," +
        "\"saleMode\":\"SUBSCRIPTION\"," +
        "\"source\":\"subscription\"," +
        "\"expiresAt\":\"2026-06-18T12:00:00.000Z\"," +
        "\"subscriptionStatus\":\"past_due\"," +
        "\"status\":\"active\"," +
        "\"licenseKey\":\"OTMK-SUB-PAST-DUE\"," +
        "\"licenseKeys\":[\"OTMK-SUB-PAST-DUE\"]," +
        "\"acquiredAt\":\"2026-03-04T00:00:00.000Z\"" +
      "}," +
      "{" +
        "\"productId\":\"product-sub-expired\"," +
        "\"packId\":\"pack-sub-expired\"," +
        "\"packIds\":[\"pack-sub-expired\"]," +
        "\"saleMode\":\"SUBSCRIPTION\"," +
        "\"source\":\"subscription\"," +
        "\"expiresAt\":\"2026-06-04T11:00:00.000Z\"," +
        "\"subscriptionStatus\":\"active\"," +
        "\"status\":\"active\"," +
        "\"licenseKey\":\"OTMK-SUB-EXPIRED\"," +
        "\"licenseKeys\":[\"OTMK-SUB-EXPIRED\"]," +
        "\"acquiredAt\":\"2026-03-05T00:00:00.000Z\"" +
      "}" +
    "]," +
    "\"trials\":[" +
      "{" +
        "\"productId\":\"product-trial-active\"," +
        "\"packId\":\"pack-trial-active\"," +
        "\"packIds\":[\"pack-trial-active\"]," +
        "\"startedAt\":\"2026-06-01T12:00:00.000Z\"," +
        "\"expiresAt\":\"2026-06-18T12:00:00.000Z\"," +
        "\"status\":\"active\"" +
        activeTrialPacks +
      "}," +
      "{" +
        "\"productId\":\"product-trial-expired\"," +
        "\"packId\":\"pack-trial-expired\"," +
        "\"packIds\":[\"pack-trial-expired\"]," +
        "\"startedAt\":\"2026-05-01T12:00:00.000Z\"," +
        "\"expiresAt\":\"2026-06-04T11:00:00.000Z\"," +
        "\"status\":\"expired\"" +
      "}" +
    "]" +
  "}";
}

std::string snapshotToken(
  const KeyPair& keyPair,
  const std::string& payload = snapshotPayloadJson(),
  const std::string& kid = ""
) {
  if (!kid.empty()) {
    return signToken(keyPair, payload, headerJsonWithKid(kid));
  }
  return signToken(keyPair, payload);
}

std::filesystem::path tempCachePath(const std::string& testName) {
  static int counter = 0;
  const auto directory = std::filesystem::temp_directory_path() /
    ("otomarket-license-sdk-" + testName + "-" + std::to_string(++counter));
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  return directory / "license.json";
}

void writeCache(
  const std::filesystem::path& path,
  const std::string& productId,
  const std::string& licenseKey,
  const std::string& machineId,
  const std::string& token
) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << "{"
         << "\"version\":1,"
         << "\"productId\":" << jsonString(productId) << ","
         << "\"licenseKey\":" << jsonString(licenseKey) << ","
         << "\"machineId\":" << jsonString(machineId) << ","
         << "\"license\":" << jsonString(token)
         << "}";
}

std::filesystem::path keysetCachePathFor(const std::filesystem::path& cachePath) {
  auto path = cachePath;
  path += ".keys";
  return path;
}

std::string keysetJson(const std::string& kid, const std::string& publicKeyPem) {
  return std::string("{\"keys\":[{\"kid\":") + jsonString(kid) +
    ",\"publicKeyPem\":" + jsonString(escapedNewlinePem(publicKeyPem)) + "}]}";
}

void writeKeysetCache(
  const std::filesystem::path& cachePath,
  const std::string& kid,
  const std::string& publicKeyPem,
  std::chrono::system_clock::time_point fetchedAt = now()
) {
  const auto path = keysetCachePathFor(cachePath);
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << "{\"keys\":{"
         << jsonString(kid) << ":" << jsonString(escapedNewlinePem(publicKeyPem))
         << "},\"fetchedAt\":"
         << static_cast<long long>(std::chrono::system_clock::to_time_t(fetchedAt))
         << "}";
}

Config makeConfig(
  const KeyPair& keyPair,
  const std::filesystem::path& cachePath,
  std::shared_ptr<HttpTransport> http = nullptr
) {
  Config config;
  config.baseUrl = "https://staging.example.test/api/license/v1";
  config.publicKeyPem = keyPair.publicKeyPem;
  config.cachePath = cachePath;
  config.http = std::move(http);
  config.machineId = "machine-1";
  config.clock = [] {
    return now();
  };
  return config;
}

std::string okActivateJson(const std::string& token) {
  return std::string("{\"ok\":true,\"license\":") + jsonString(token) +
    ",\"seatsUsed\":1,\"maxActivations\":2,\"expiresAt\":null}";
}

std::string okTrialJson(const std::string& token, const std::string& expiresAt) {
  return std::string("{\"ok\":true,\"license\":") + jsonString(token) +
    ",\"expiresAt\":" + expiresAt +
    ",\"isTrial\":true,\"seatsUsed\":1,\"maxActivations\":1}";
}

std::string okVerifyJson(const std::string& token) {
  return std::string("{\"ok\":true,\"status\":\"ACTIVE\",\"expiresAt\":null,\"license\":") +
    jsonString(token) + "}";
}

void verifiesGeneratedEd25519Token() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = licenseToken(keyPair);

  auto result = otomarket::license::verifyLicenseToken(
    token,
    keyPair.publicKeyPem,
    "product-1",
    "machine-1",
    now()
  );

  require(result.ok, "generated Ed25519 token should verify");
  require(result.license.has_value(), "verified token should expose payload");
  requireEqual(result.license->licenseId, std::string("license-1"), "licenseId mismatch");
  requireEqual(result.license->maxActivations, 2, "maxActivations mismatch");
  require(!result.license->isTrial, "token without isTrial should default to false");
}

void parsesTrialTokenFlag() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = licenseToken(
    keyPair,
    "product-1",
    "machine-1",
    kFutureIso,
    "null",
    kNowIso,
    "",
    ",\"isTrial\":true"
  );

  auto result = otomarket::license::verifyLicenseToken(
    token,
    keyPair.publicKeyPem,
    "product-1",
    "machine-1",
    now()
  );

  require(result.ok, "trial token should verify");
  require(result.license.has_value(), "trial token should expose payload");
  require(result.license->isTrial, "isTrial true should be exposed");
}

void verifiesPublicKeyFormatsNormalizeToSameEd25519Key() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = licenseToken(keyPair);
  const std::vector<std::pair<std::string, std::string>> publicKeys = {
    {"multi-line PEM", keyPair.publicKeyPem},
    {"escaped-newline PEM", escapedNewlinePem(keyPair.publicKeyPem)},
    {"compact PEM", compactPem(keyPair.publicKeyPem)},
    {"base64 body", publicKeyBase64BodyFromPem(keyPair.publicKeyPem)},
    {"hex", publicKeyHexFromRawKey(keyPair.publicKey)},
  };

  for (const auto& publicKey : publicKeys) {
    auto result = otomarket::license::verifyLicenseToken(
      token,
      publicKey.second,
      "product-1",
      "machine-1",
      now()
    );

    require(result.ok, publicKey.first + " public key should verify the same token");
    require(result.license.has_value(), publicKey.first + " public key should expose payload");
    requireEqual(result.license->licenseId, std::string("license-1"), publicKey.first + " licenseId mismatch");
  }
}

void rejectsTamperedToken() {
  const KeyPair keyPair = generateKeyPair();
  std::string token = licenseToken(keyPair);
  const size_t secondPart = token.find('.') + 1;
  token[secondPart + 3] = token[secondPart + 3] == 'a' ? 'b' : 'a';

  auto result = otomarket::license::verifyLicenseToken(
    token,
    keyPair.publicKeyPem,
    "product-1",
    "machine-1",
    now()
  );

  require(!result.ok, "tampered token should fail verification");
  requireEqual(result.error, ErrorCode::InvalidSignature, "tamper error should be invalid signature");
}

void rejectsExpiredToken() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = licenseToken(
    keyPair,
    "product-1",
    "machine-1",
    kFutureIso,
    "\"2026-06-04T11:59:59.000Z\""
  );

  auto result = otomarket::license::verifyLicenseToken(
    token,
    keyPair.publicKeyPem,
    "product-1",
    "machine-1",
    now()
  );

  require(!result.ok, "expired token should fail");
  requireEqual(result.error, ErrorCode::Expired, "expired token should report EXPIRED");
}

void rejectsTokenIssuedTooFarInFuture() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = licenseToken(
    keyPair,
    "product-1",
    "machine-1",
    kFutureIso,
    "null",
    "2026-06-05T12:00:01.000Z"
  );

  auto result = otomarket::license::verifyLicenseToken(
    token,
    keyPair.publicKeyPem,
    "product-1",
    "machine-1",
    now()
  );

  require(!result.ok, "token issued more than 24h in the future should fail");
  requireEqual(result.error, ErrorCode::InvalidSignature, "future issuedAt should report invalid signature");
}

void capsOfflineTrustAtMaxOffline() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("max-offline-cap");
  const std::string farFutureVerifyAfter = "2026-12-31T12:00:00.000Z";
  const std::string token = licenseToken(keyPair, "product-1", "machine-1", farFutureVerifyAfter);
  writeCache(cachePath, "product-1", "license-key-1", "machine-1", token);

  auto httpWithinCap = std::make_shared<MockHttpTransport>();
  Config withinCapConfig = makeConfig(keyPair, cachePath, httpWithinCap);
  withinCapConfig.maxOffline = std::chrono::hours(1);
  withinCapConfig.verifyRetryGrace = std::chrono::minutes(30);
  withinCapConfig.clock = [] {
    return makeTime(2026, 6, 4, 12, 30, 0);
  };

  Client withinCap(withinCapConfig);
  require(withinCap.otoIsLicensed(), "token should remain licensed inside maxOffline cap");
  requireEqual(withinCap.state(), LicenseState::Licensed, "inside-cap state mismatch");
  requireEqual(httpWithinCap->requests.size(), static_cast<size_t>(0), "inside cap should not verify online");

  auto httpAfterGrace = std::make_shared<MockHttpTransport>();
  httpAfterGrace->handler = [](const HttpRequest&) {
    return HttpResponse{503, "{}"};
  };

  Config afterGraceConfig = makeConfig(keyPair, cachePath, httpAfterGrace);
  afterGraceConfig.maxOffline = std::chrono::hours(1);
  afterGraceConfig.verifyRetryGrace = std::chrono::minutes(30);
  afterGraceConfig.clock = [] {
    return makeTime(2026, 6, 4, 14, 0, 0);
  };

  Client afterGrace(afterGraceConfig);
  require(!afterGrace.otoIsLicensed(), "token should not stay licensed after maxOffline cap and retry grace");
  requireEqual(afterGrace.state(), LicenseState::NetworkUnavailable, "after-grace state mismatch");
  requireEqual(httpAfterGrace->requests.size(), static_cast<size_t>(1), "after cap should try one online verify");
}

void verifiesKidTokenWithKeyset() {
  const KeyPair legacyKey = generateKeyPair();
  const KeyPair keysetKey = generateKeyPair();
  const std::string token = licenseToken(
    keysetKey,
    "product-1",
    "machine-1",
    kFutureIso,
    "null",
    kNowIso,
    "key-2026"
  );
  const std::map<std::string, std::string> keyset = {
    {"key-2026", keysetKey.publicKeyPem},
  };

  auto result = otomarket::license::verifyLicenseToken(
    token,
    legacyKey.publicKeyPem,
    "product-1",
    "machine-1",
    now(),
    keyset
  );

  require(result.ok, "kid token should verify with matching keyset key");
  require(result.license.has_value(), "kid token should expose payload");
}

void legacyTokenWithoutKidUsesPublicKeyPem() {
  const KeyPair legacyKey = generateKeyPair();
  const KeyPair keysetKey = generateKeyPair();
  const std::string token = licenseToken(legacyKey);
  const std::map<std::string, std::string> keyset = {
    {"key-2026", keysetKey.publicKeyPem},
  };

  auto result = otomarket::license::verifyLicenseToken(
    token,
    legacyKey.publicKeyPem,
    "product-1",
    "machine-1",
    now(),
    keyset
  );

  require(result.ok, "token without kid should keep using legacy publicKeyPem");
  require(result.license.has_value(), "legacy token should expose payload");
}

void rejectsKidMissingFromKeyset() {
  const KeyPair legacyKey = generateKeyPair();
  const KeyPair keysetKey = generateKeyPair();
  const std::string token = licenseToken(
    keysetKey,
    "product-1",
    "machine-1",
    kFutureIso,
    "null",
    kNowIso,
    "key-2026"
  );
  const std::map<std::string, std::string> keyset = {
    {"other-key", keysetKey.publicKeyPem},
  };

  auto result = otomarket::license::verifyLicenseToken(
    token,
    legacyKey.publicKeyPem,
    "product-1",
    "machine-1",
    now(),
    keyset
  );

  require(!result.ok, "kid token should fail when keyset omits kid");
  requireEqual(result.error, ErrorCode::InvalidSignature, "missing kid should report invalid signature");
}

void clientUsesConfigKeysetForCachedKidToken() {
  const KeyPair legacyKey = generateKeyPair();
  const KeyPair keysetKey = generateKeyPair();
  const auto cachePath = tempCachePath("client-keyset");
  const std::string staleToken = licenseToken(
    keysetKey,
    "product-1",
    "machine-1",
    kNowIso,
    "null",
    kNowIso,
    "key-2026"
  );
  const std::string refreshedToken = licenseToken(
    keysetKey,
    "product-1",
    "machine-1",
    kFutureIso,
    "null",
    kNowIso,
    "key-2026"
  );
  writeCache(cachePath, "product-1", "license-key-1", "machine-1", staleToken);

  auto http = std::make_shared<MockHttpTransport>();
  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/verify") != std::string::npos, "client keyset verify URL mismatch");
    return HttpResponse{200, okVerifyJson(refreshedToken)};
  };

  Config config = makeConfig(legacyKey, cachePath, http);
  config.keyset = {
    {"key-2026", keysetKey.publicKeyPem},
  };

  Client client(config);
  require(client.otoIsLicensed(), "client should verify cached and refreshed kid tokens using config keyset");
  requireEqual(client.state(), LicenseState::Licensed, "client keyset state mismatch");
  requireEqual(http->requests.size(), static_cast<size_t>(1), "client keyset should verify online once");
}

void clientUsesConfigKeysetForActivationToken() {
  const KeyPair legacyKey = generateKeyPair();
  const KeyPair keysetKey = generateKeyPair();
  const auto cachePath = tempCachePath("activate-keyset");
  const std::string token = licenseToken(
    keysetKey,
    "product-1",
    "machine-1",
    kFutureIso,
    "null",
    kNowIso,
    "key-2026"
  );

  auto http = std::make_shared<MockHttpTransport>();
  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/activate") != std::string::npos, "activation keyset URL mismatch");
    return HttpResponse{200, okActivateJson(token)};
  };

  Config config = makeConfig(legacyKey, cachePath, http);
  config.keyset = {
    {"key-2026", keysetKey.publicKeyPem},
  };

  Client client(config);
  const ActivateResult activated = client.otoActivate("product-1", "license-key-1");
  require(activated.ok, "activation should verify kid token using config keyset");
  requireEqual(client.state(), LicenseState::Licensed, "activation keyset state mismatch");
}

void clientFetchesKeysetWithGetForActivationKidToken() {
  const KeyPair legacyKey = generateKeyPair();
  const KeyPair keysetKey = generateKeyPair();
  const auto cachePath = tempCachePath("activate-fetch-keyset");
  const std::string token = licenseToken(
    keysetKey,
    "product-1",
    "machine-1",
    kFutureIso,
    "null",
    kNowIso,
    "key-2026"
  );

  auto http = std::make_shared<MockHttpTransport>();
  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/activate") != std::string::npos, "activation fetch URL mismatch");
    return HttpResponse{200, okActivateJson(token)};
  };
  http->getHandler = [&](const HttpRequest& request) {
    require(request.url.find("/keys") != std::string::npos, "keyset fetch URL mismatch");
    return HttpResponse{200, keysetJson("key-2026", keysetKey.publicKeyPem)};
  };

  Client client(makeConfig(legacyKey, cachePath, http));
  const ActivateResult activated = client.otoActivate("product-1", "license-key-1");

  require(activated.ok, "activation should fetch keyset and verify kid token");
  requireEqual(client.state(), LicenseState::Licensed, "activation fetch keyset state mismatch");
  requireEqual(http->requests.size(), static_cast<size_t>(1), "activation should issue one POST");
  requireEqual(http->getRequests.size(), static_cast<size_t>(1), "activation should fetch /keys once");
  require(std::filesystem::exists(keysetCachePathFor(cachePath)), "activation should persist keyset cache");
}

void keysUnsupportedStillAllowsLegacyActivationToken() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("keys-unsupported-legacy");
  const std::string token = licenseToken(keyPair);

  auto http = std::make_shared<MockHttpTransport>();
  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/activate") != std::string::npos, "legacy activation URL mismatch");
    return HttpResponse{200, okActivateJson(token)};
  };

  Client client(makeConfig(keyPair, cachePath, http));
  const ActivateResult activated = client.otoActivate("product-1", "license-key-1");

  require(activated.ok, "legacy activation should ignore unsupported /keys");
  requireEqual(client.state(), LicenseState::Licensed, "legacy activation state mismatch");
  requireEqual(http->getRequests.size(), static_cast<size_t>(1), "legacy activation should best-effort fetch /keys");
}

void cachedKeysetKeepsLicenseWhenRefreshGetFails() {
  const KeyPair legacyKey = generateKeyPair();
  const KeyPair keysetKey = generateKeyPair();
  const auto cachePath = tempCachePath("cached-keyset-get-fails");
  const std::string staleToken = licenseToken(
    keysetKey,
    "product-1",
    "machine-1",
    kPastIso,
    "null",
    kNowIso,
    "key-2026"
  );
  const std::string refreshedToken = licenseToken(
    keysetKey,
    "product-1",
    "machine-1",
    kFutureIso,
    "null",
    kNowIso,
    "key-2026"
  );
  writeCache(cachePath, "product-1", "license-key-1", "machine-1", staleToken);
  writeKeysetCache(
    cachePath,
    "key-2026",
    keysetKey.publicKeyPem,
    makeTime(2026, 6, 2, 12, 0, 0)
  );

  auto http = std::make_shared<MockHttpTransport>();
  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/verify") != std::string::npos, "cached keyset verify URL mismatch");
    return HttpResponse{200, okVerifyJson(refreshedToken)};
  };
  http->getHandler = [](const HttpRequest&) -> HttpResponse {
    throw std::runtime_error("offline keyset fetch");
  };

  Config config = makeConfig(legacyKey, cachePath, http);
  config.keysetTtl = std::chrono::hours(1);
  Client client(config);

  require(client.otoIsLicensed(), "cached keyset should keep license when keyset refresh fails");
  requireEqual(client.state(), LicenseState::Licensed, "cached keyset refresh failure state mismatch");
  requireEqual(http->requests.size(), static_cast<size_t>(1), "cached keyset should verify online once");
  requireEqual(http->getRequests.size(), static_cast<size_t>(1), "stale keyset should attempt one refresh");
}

void fileKeysetCacheRestoresForNewClient() {
  const KeyPair legacyKey = generateKeyPair();
  const KeyPair keysetKey = generateKeyPair();
  const auto cachePath = tempCachePath("file-keyset-restore");
  const std::string token = licenseToken(
    keysetKey,
    "product-1",
    "machine-1",
    kFutureIso,
    "null",
    kNowIso,
    "key-2026"
  );
  writeCache(cachePath, "product-1", "license-key-1", "machine-1", token);
  writeKeysetCache(cachePath, "key-2026", keysetKey.publicKeyPem);

  Client client(makeConfig(legacyKey, cachePath));

  require(client.otoIsLicensed(), "new client should restore keyset cache from file");
  requireEqual(client.state(), LicenseState::Licensed, "file keyset restore state mismatch");
}

void cachesRoundTripOffline() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("roundtrip");
  const std::string token = licenseToken(keyPair);
  auto http = std::make_shared<MockHttpTransport>();
  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/activate") != std::string::npos, "activate URL mismatch");
    require(request.body.find("\"productKey\":\"product-1\"") != std::string::npos, "productKey omitted");
    require(request.body.find("\"licenseKey\":\"license-key-1\"") != std::string::npos, "licenseKey omitted");
    return HttpResponse{200, okActivateJson(token)};
  };

  Client activating(makeConfig(keyPair, cachePath, http));
  ActivateResult activated = activating.otoActivate("product-1", "license-key-1", "Studio Mac");
  require(activated.ok, "activation should succeed");
  require(std::filesystem::exists(cachePath), "activation should write cache");

  Client offline(makeConfig(keyPair, cachePath));
  require(offline.otoIsLicensed(), "fresh cache should license offline");
  requireEqual(offline.state(), LicenseState::Licensed, "fresh cache state mismatch");
}

void verifyAfterBoundaryTriggersOnlineVerify() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("verify-after");
  const std::string staleToken = licenseToken(keyPair, "product-1", "machine-1", kNowIso);
  const std::string refreshedToken = licenseToken(keyPair, "product-1", "machine-1", kFutureIso);
  writeCache(cachePath, "product-1", "license-key-1", "machine-1", staleToken);

  auto http = std::make_shared<MockHttpTransport>();
  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/verify") != std::string::npos, "verify URL mismatch");
    return HttpResponse{200, okVerifyJson(refreshedToken)};
  };

  Client client(makeConfig(keyPair, cachePath, http));
  require(client.otoIsLicensed(), "verifyAfter boundary should refresh online");
  requireEqual(http->requests.size(), static_cast<size_t>(1), "verify should be called once");
  requireEqual(client.state(), LicenseState::Licensed, "refreshed state mismatch");
}

void offlineGraceAllowsTransientVerifyFailure() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("grace");
  const std::string staleToken = licenseToken(keyPair, "product-1", "machine-1", kPastIso);
  writeCache(cachePath, "product-1", "license-key-1", "machine-1", staleToken);

  auto http = std::make_shared<MockHttpTransport>();
  http->handler = [](const HttpRequest&) {
    return HttpResponse{503, "{}"};
  };

  Config config = makeConfig(keyPair, cachePath, http);
  config.verifyRetryGrace = std::chrono::hours(2);
  Client client(config);

  require(client.otoIsLicensed(), "network failure inside retry grace should stay licensed");
  requireEqual(client.state(), LicenseState::LicensedOfflineGrace, "offline grace state mismatch");
}

void offlineGraceExpiresAfterBoundary() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("grace-expired");
  const std::string staleToken = licenseToken(keyPair, "product-1", "machine-1", kOlderPastIso);
  writeCache(cachePath, "product-1", "license-key-1", "machine-1", staleToken);

  auto http = std::make_shared<MockHttpTransport>();
  http->handler = [](const HttpRequest&) {
    return HttpResponse{503, "{}"};
  };

  Config config = makeConfig(keyPair, cachePath, http);
  config.verifyRetryGrace = std::chrono::hours(2);
  Client client(config);

  require(!client.otoIsLicensed(), "network failure outside retry grace should not license");
  requireEqual(client.state(), LicenseState::NetworkUnavailable, "expired grace state mismatch");
}

void machineIdDerivationIsStable() {
  const std::string first = otomarket::license::deriveMachineId("product-1", "host=studio\nuser=alice\n");
  const std::string second = otomarket::license::deriveMachineId("product-1", "host=studio\nuser=alice\n");
  const std::string other = otomarket::license::deriveMachineId("product-2", "host=studio\nuser=alice\n");

  requireEqual(first, second, "machineId should be stable for same inputs");
  require(first != other, "machineId should include product salt");
  require(first.rfind("oto2_", 0) == 0, "machineId should use oto2 prefix");
}

void activateVerifyDeactivateFlowUsesMockHttp() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("flow");
  const std::string activationToken = licenseToken(keyPair, "product-1", "machine-1", kPastIso);
  const std::string refreshedToken = licenseToken(keyPair, "product-1", "machine-1", kFutureIso);
  auto http = std::make_shared<MockHttpTransport>();

  http->handler = [&](const HttpRequest& request) {
    if (request.url.find("/activate") != std::string::npos) {
      return HttpResponse{200, okActivateJson(activationToken)};
    }
    if (request.url.find("/verify") != std::string::npos) {
      return HttpResponse{200, okVerifyJson(refreshedToken)};
    }
    if (request.url.find("/deactivate") != std::string::npos) {
      return HttpResponse{200, R"({"ok":true,"seatsUsed":0})"};
    }
    return HttpResponse{404, "{}"};
  };

  Client client(makeConfig(keyPair, cachePath, http));
  const ActivateResult activated = client.otoActivate("product-1", "license-key-1");
  require(activated.ok, "activate should succeed");
  require(client.otoIsLicensed(), "verify should refresh stale activation token");

  const auto deactivated = client.otoDeactivate();
  require(deactivated.ok, "deactivate should succeed");
  requireEqual(deactivated.seatsUsed, 0, "seatsUsed mismatch");
  require(!std::filesystem::exists(cachePath), "deactivate should remove cache");
  requireEqual(http->requests.size(), static_cast<size_t>(3), "flow should issue three HTTP calls");
}

void activateOmitsEmptyMachineName() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("activate-empty-machine-name");
  const std::string activationToken = licenseToken(keyPair, "product-1", "machine-1", kFutureIso);
  auto http = std::make_shared<MockHttpTransport>();

  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/activate") != std::string::npos, "activate URL mismatch");
    return HttpResponse{200, okActivateJson(activationToken)};
  };

  Client client(makeConfig(keyPair, cachePath, http));
  const ActivateResult activated = client.otoActivate("product-1", "license-key-1");

  require(activated.ok, "activate should succeed without machineName");
  requireEqual(http->requests.size(), static_cast<size_t>(1), "activate should issue one HTTP call");
  require(
    http->requests.front().body.find("\"machineName\"") == std::string::npos,
    "empty machineName should be omitted from activate body"
  );
}

void activateSendsProvidedMachineName() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("activate-provided-machine-name");
  const std::string activationToken = licenseToken(keyPair, "product-1", "machine-1", kFutureIso);
  auto http = std::make_shared<MockHttpTransport>();

  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/activate") != std::string::npos, "activate URL mismatch");
    return HttpResponse{200, okActivateJson(activationToken)};
  };

  Client client(makeConfig(keyPair, cachePath, http));
  const ActivateResult activated = client.otoActivate("product-1", "license-key-1", "Studio Mac");

  require(activated.ok, "activate should succeed with machineName");
  requireEqual(http->requests.size(), static_cast<size_t>(1), "activate should issue one HTTP call");
  require(
    http->requests.front().body.find("\"machineName\":\"Studio Mac\"") != std::string::npos,
    "provided machineName should be sent in activate body"
  );
}

void http400ApiErrorClassifiesAsRequestRejected() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("http-400-request-rejected");
  auto http = std::make_shared<MockHttpTransport>();

  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/activate") != std::string::npos, "activate URL mismatch");
    return HttpResponse{
      400,
      R"({"error":"Invalid request body.","code":"INVALID_REQUEST_BODY"})"
    };
  };

  Client client(makeConfig(keyPair, cachePath, http));
  const ActivateResult activated = client.otoActivate("product-1", "license-key-1");

  require(!activated.ok, "HTTP 400 should fail activation");
  requireEqual(
    activated.error,
    ErrorCode::RequestRejected,
    "HTTP 400 API error should be request rejected"
  );
  require(activated.error != ErrorCode::NetworkError, "HTTP 400 should not be network error");
  requireEqual(client.state(), LicenseState::Error, "HTTP 400 state mismatch");
  requireEqual(client.lastError(), ErrorCode::RequestRejected, "HTTP 400 last error mismatch");
  require(
    activated.errorMessage.find("Invalid request body.") != std::string::npos,
    "HTTP 400 message should include server error"
  );
  require(
    activated.errorMessage.find("HTTP 400") != std::string::npos,
    "HTTP 400 message should include status"
  );
  requireEqual(
    otomarket::license::toString(activated.error),
    std::string("REQUEST_REJECTED"),
    "request rejected string mismatch"
  );
}

void http409SeatLimitClassifiesAsSeatLimit() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("http-409-seat-limit");
  auto http = std::make_shared<MockHttpTransport>();

  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/activate") != std::string::npos, "activate URL mismatch");
    return HttpResponse{
      409,
      R"({"error":"Activation limit reached.","code":"SEAT_LIMIT"})"
    };
  };

  Client client(makeConfig(keyPair, cachePath, http));
  const ActivateResult activated = client.otoActivate("product-1", "license-key-1");

  require(!activated.ok, "HTTP 409 should fail activation");
  requireEqual(activated.error, ErrorCode::SeatLimit, "HTTP 409 code should map to SeatLimit");
  require(activated.error != ErrorCode::NetworkError, "HTTP 409 should not be network error");
  requireEqual(client.state(), LicenseState::SeatLimit, "HTTP 409 state mismatch");
  requireEqual(client.lastError(), ErrorCode::SeatLimit, "HTTP 409 last error mismatch");
}

void startTrialCachesTrialToken() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("start-trial");
  const std::string trialToken = licenseToken(
    keyPair,
    "product-1",
    "machine-1",
    kFutureIso,
    jsonString(kFutureIso),
    kNowIso,
    "",
    ",\"isTrial\":true"
  );
  auto http = std::make_shared<MockHttpTransport>();

  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/trial") != std::string::npos, "trial URL mismatch");
    require(
      request.body.find("\"productKey\":\"product-1\"") != std::string::npos,
      "trial request should send productKey"
    );
    require(
      request.body.find("\"machineId\":\"machine-1\"") != std::string::npos,
      "trial request should send machineId"
    );
    require(
      request.body.find("\"licenseKey\"") == std::string::npos,
      "trial request should omit licenseKey"
    );
    require(
      request.body.find("\"machineName\"") == std::string::npos,
      "trial request should omit machineName"
    );
    return HttpResponse{200, okTrialJson(trialToken, jsonString(kFutureIso))};
  };

  Client client(makeConfig(keyPair, cachePath, http));
  const ActivateResult started = client.otoStartTrial("product-1");

  require(started.ok, "startTrial should succeed");
  requireEqual(started.seatsUsed, 1, "trial seatsUsed mismatch");
  requireEqual(started.maxActivations, 1, "trial maxActivations mismatch");
  require(started.expiresAt.has_value(), "trial should expose expiresAt");
  require(std::filesystem::exists(cachePath), "startTrial should cache the token");

  const auto cached = client.cachedLicense();
  require(cached.has_value(), "startTrial should expose cached license");
  require(cached->isTrial, "cached trial license should expose isTrial");
  require(client.otoIsLicensed(), "cached trial token should license offline");
  requireEqual(http->requests.size(), static_cast<size_t>(1), "startTrial should issue one POST");
}

void startTrialReturnsServerTrialError() {
  const KeyPair keyPair = generateKeyPair();
  const auto cachePath = tempCachePath("start-trial-error");
  auto http = std::make_shared<MockHttpTransport>();

  http->handler = [&](const HttpRequest& request) {
    require(request.url.find("/trial") != std::string::npos, "trial error URL mismatch");
    return HttpResponse{200, R"({"ok":false,"error":"TRIAL_NOT_AVAILABLE"})"};
  };

  Client client(makeConfig(keyPair, cachePath, http));
  const ActivateResult started = client.otoStartTrial("product-1");

  require(!started.ok, "startTrial should fail for server trial error");
  requireEqual(started.error, ErrorCode::TrialNotAvailable, "trial error code mismatch");
  requireEqual(started.errorMessage, std::string("TRIAL_NOT_AVAILABLE"), "trial error message mismatch");
  requireEqual(
    otomarket::license::toString(started.error),
    std::string("TRIAL_NOT_AVAILABLE"),
    "trial error string mismatch"
  );
  require(!std::filesystem::exists(cachePath), "failed startTrial should not write cache");
}

void verifiesGeneratedLicenseSnapshot() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = snapshotToken(keyPair);

  const auto result = otomarket::license::verifyLicenseSnapshot(
    token,
    keyPair.publicKeyPem
  );

  require(result.ok, "generated license snapshot should verify");
  require(result.snapshot.has_value(), "verified snapshot should expose payload");

  const auto& snapshot = *result.snapshot;
  requireEqual(snapshot.schemaVersion, 2, "snapshot schemaVersion mismatch");
  requireEqual(snapshot.issuer, std::string("OtoMarket"), "snapshot issuer mismatch");
  requireEqual(snapshot.sub, std::string("user-1"), "snapshot sub mismatch");
  requireEqual(snapshot.offlineGracePeriodDays, 7, "snapshot offlineGracePeriodDays mismatch");
  requireEqual(snapshot.entitlements.size(), static_cast<size_t>(5), "snapshot entitlement count mismatch");
  requireEqual(snapshot.trials.size(), static_cast<size_t>(2), "snapshot trial count mismatch");

  const auto& buyout = snapshot.entitlements[0];
  requireEqual(buyout.productId, std::string("product-buyout"), "buyout productId mismatch");
  require(buyout.packId.has_value(), "buyout packId should parse");
  requireEqual(*buyout.packId, std::string("pack-buyout"), "buyout packId mismatch");
  requireEqual(buyout.packIds.size(), static_cast<size_t>(2), "buyout packIds count mismatch");
  requireEqual(buyout.source, SnapshotSource::Buyout, "buyout source mismatch");
  require(!buyout.expiresAt.has_value(), "buyout should have no expiresAt");
  requireEqual(buyout.subscriptionStatus, SubscriptionStatus::None, "buyout subscription status mismatch");
  requireEqual(buyout.licenseKeys.front(), std::string("OTMK-BUYOUT"), "buyout licenseKeys mismatch");

  const auto& freeEntitlement = snapshot.entitlements[1];
  requireEqual(freeEntitlement.source, SnapshotSource::Free, "free source mismatch");

  const auto& activeSub = snapshot.entitlements[2];
  requireEqual(activeSub.source, SnapshotSource::Subscription, "active sub source mismatch");
  requireEqual(activeSub.subscriptionStatus, SubscriptionStatus::Active, "active sub status mismatch");
  require(activeSub.expiresAt.has_value(), "active sub expiresAt should parse");
  require(*activeSub.expiresAt == makeTime(2026, 6, 18, 12, 0, 0), "active sub expiresAt mismatch");

  const auto& pastDueSub = snapshot.entitlements[3];
  requireEqual(pastDueSub.subscriptionStatus, SubscriptionStatus::PastDue, "past_due status mismatch");

  requireEqual(snapshot.trials[0].status, std::string("active"), "active trial status mismatch");
  requireEqual(snapshot.trials[1].status, std::string("expired"), "expired trial status mismatch");
}

void parsesLicenseSnapshotPacks() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = snapshotToken(keyPair, snapshotPayloadJson("2", true));

  const auto result = otomarket::license::verifyLicenseSnapshot(
    token,
    keyPair.publicKeyPem
  );

  require(result.ok, "snapshot with packs should verify");
  require(result.snapshot.has_value(), "snapshot with packs should expose payload");

  const auto& snapshot = *result.snapshot;
  const auto& buyout = snapshot.entitlements[0];
  requireEqual(buyout.packs.size(), static_cast<size_t>(2), "buyout packs count mismatch");
  requireEqual(buyout.packs[0].packId, std::string("pack-buyout"), "buyout pack packId mismatch");
  require(buyout.packs[0].contentHash.has_value(), "buyout pack contentHash should parse");
  requireEqual(
    *buyout.packs[0].contentHash,
    std::string(kPackBuyoutContentHash),
    "buyout pack contentHash mismatch"
  );
  requireEqual(buyout.packs[1].packId, std::string("pack-bundle-b"), "bundle pack packId mismatch");
  require(!buyout.packs[1].contentHash.has_value(), "bundle pack null contentHash should parse");

  const auto& activeTrial = snapshot.trials[0];
  requireEqual(activeTrial.packs.size(), static_cast<size_t>(1), "active trial packs count mismatch");
  requireEqual(
    activeTrial.packs[0].packId,
    std::string("pack-trial-active"),
    "active trial packId mismatch"
  );
  require(activeTrial.packs[0].contentHash.has_value(), "active trial contentHash should parse");
  requireEqual(
    *activeTrial.packs[0].contentHash,
    std::string(kPackTrialContentHash),
    "active trial contentHash mismatch"
  );
}

void parsesLicenseSnapshotWithoutPacks() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = snapshotToken(keyPair, snapshotPayloadJson("2", false));

  const auto result = otomarket::license::verifyLicenseSnapshot(
    token,
    keyPair.publicKeyPem
  );

  require(result.ok, "snapshot without packs should verify");
  require(result.snapshot.has_value(), "snapshot without packs should expose payload");

  for (const auto& entitlement : result.snapshot->entitlements) {
    require(entitlement.packs.empty(), "missing entitlement packs should default empty");
  }
  for (const auto& trial : result.snapshot->trials) {
    require(trial.packs.empty(), "missing trial packs should default empty");
  }
}

void verifiesPackContentHashes() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = snapshotToken(keyPair, snapshotPayloadJson("2", true));
  const auto result = otomarket::license::verifyLicenseSnapshot(
    token,
    keyPair.publicKeyPem
  );
  require(result.ok && result.snapshot.has_value(), "snapshot with pack hashes should verify");

  const auto& snapshot = *result.snapshot;
  requireEqual(
    otomarket::license::verifyPackContent(
      snapshot,
      "pack-buyout",
      "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    ),
    PackContentResult::Match,
    "matching pack content hash should match case-insensitively"
  );
  requireEqual(
    otomarket::license::verifyPackContent(
      snapshot,
      "pack-buyout",
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
    ),
    PackContentResult::Mismatch,
    "different pack content hash should mismatch"
  );
  requireEqual(
    otomarket::license::verifyPackContent(snapshot, "pack-trial-active", kPackTrialContentHash),
    PackContentResult::Match,
    "trial pack content hash should match"
  );
  requireEqual(
    otomarket::license::verifyPackContent(snapshot, "pack-missing", kPackBuyoutContentHash),
    PackContentResult::Unknown,
    "missing packId should be unknown"
  );
  requireEqual(
    otomarket::license::verifyPackContent(snapshot, "pack-bundle-b", kPackBuyoutContentHash),
    PackContentResult::Unknown,
    "null contentHash should be unknown"
  );
}

void rejectsTamperedLicenseSnapshot() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = snapshotToken(keyPair);

  // Tamper the first character of the signature segment. The last base64url
  // character of an Ed25519 signature only carries 2 meaningful bits (the rest
  // is padding), so toggling it can be a no-op on decode and let the signature
  // still verify (flaky). The first signature character is fully meaningful.
  std::string signatureTampered = token;
  const size_t signatureStart = token.rfind('.') + 1;
  signatureTampered[signatureStart] =
    signatureTampered[signatureStart] == 'A' ? 'B' : 'A';

  auto signatureResult = otomarket::license::verifyLicenseSnapshot(
    signatureTampered,
    keyPair.publicKeyPem
  );
  require(!signatureResult.ok, "tampered snapshot signature should fail");
  requireEqual(
    signatureResult.error,
    ErrorCode::InvalidSignature,
    "tampered snapshot signature should report invalid signature"
  );

  std::string payloadTampered = token;
  const size_t payloadStart = payloadTampered.find('.') + 1;
  payloadTampered[payloadStart + 3] =
    payloadTampered[payloadStart + 3] == 'a' ? 'b' : 'a';

  auto payloadResult = otomarket::license::verifyLicenseSnapshot(
    payloadTampered,
    keyPair.publicKeyPem
  );
  require(!payloadResult.ok, "tampered snapshot payload should fail");
  requireEqual(
    payloadResult.error,
    ErrorCode::InvalidSignature,
    "tampered snapshot payload should report invalid signature"
  );
}

void rejectsLicenseSnapshotSignedByAnotherKey() {
  const KeyPair signingKey = generateKeyPair();
  const KeyPair otherKey = generateKeyPair();
  const std::string token = snapshotToken(signingKey);

  const auto result = otomarket::license::verifyLicenseSnapshot(
    token,
    otherKey.publicKeyPem
  );

  require(!result.ok, "snapshot should fail with a different public key");
  requireEqual(result.error, ErrorCode::InvalidSignature, "different key error mismatch");
}

void verifiesLicenseSnapshotKidKeyset() {
  const KeyPair keyPair = generateKeyPair();
  const KeyPair otherKey = generateKeyPair();
  const std::string token = snapshotToken(keyPair, snapshotPayloadJson(), "key-2026");

  const std::map<std::string, std::string> keyset = {
    {"other-key", otherKey.publicKeyPem},
    {"key-2026", keyPair.publicKeyPem},
  };

  auto result = otomarket::license::verifyLicenseSnapshot(token, keyset);
  require(result.ok, "snapshot kid should select matching keyset key");
  require(result.snapshot.has_value(), "kid snapshot should expose payload");

  const std::map<std::string, std::string> missingKeyset = {
    {"other-key", otherKey.publicKeyPem},
  };
  auto missingResult = otomarket::license::verifyLicenseSnapshot(token, missingKeyset);
  require(!missingResult.ok, "snapshot kid should fail when keyset omits kid");
  requireEqual(missingResult.error, ErrorCode::InvalidSignature, "missing kid error mismatch");

  const std::string noKidToken = snapshotToken(keyPair);
  const std::map<std::string, std::string> singleKeyset = {
    {"only-key", keyPair.publicKeyPem},
  };
  auto noKidResult = otomarket::license::verifyLicenseSnapshot(noKidToken, singleKeyset);
  require(noKidResult.ok, "snapshot without kid should use single-entry keyset");
}

void evaluatePackUsesSnapshotFacts() {
  const KeyPair keyPair = generateKeyPair();
  const auto verified = otomarket::license::verifyLicenseSnapshot(
    snapshotToken(keyPair),
    keyPair.publicKeyPem
  );
  require(verified.ok && verified.snapshot.has_value(), "snapshot fixture should verify");
  const auto& snapshot = *verified.snapshot;

  const auto current = makeTime(2026, 6, 4, 12, 0, 0);
  const auto buyout = otomarket::license::evaluatePack(snapshot, "pack-buyout", current);
  require(buyout.entitled, "owned buyout pack should be entitled");
  requireEqual(buyout.source, SnapshotSource::Buyout, "buyout access source mismatch");
  require(!buyout.expiresAt.has_value(), "buyout access should be unlimited");
  require(buyout.snapshotFresh, "current snapshot should be fresh at iat boundary");
  require(buyout.withinOfflineGrace, "current snapshot should be inside offline grace");
  require(buyout.matchedProductId.has_value(), "buyout access should expose matched product");
  requireEqual(*buyout.matchedProductId, std::string("product-buyout"), "buyout matched product mismatch");

  const auto missing = otomarket::license::evaluatePack(snapshot, "pack-missing", current);
  require(!missing.entitled, "missing pack should not be entitled");
  require(missing.snapshotFresh, "missing pack should still report snapshot freshness");
  require(missing.withinOfflineGrace, "missing pack should still report offline grace");

  const auto activeSub = otomarket::license::evaluatePack(snapshot, "pack-sub-active", current);
  require(activeSub.entitled, "active subscription pack should match");
  requireEqual(activeSub.source, SnapshotSource::Subscription, "active subscription source mismatch");
  require(activeSub.expiresAt.has_value(), "active subscription should expose expiresAt");
  require(*activeSub.expiresAt > current, "active subscription expiresAt should be in the future");

  const auto expiredSub = otomarket::license::evaluatePack(snapshot, "pack-sub-expired", current);
  require(expiredSub.entitled, "expired subscription pack should still report a signed match");
  require(expiredSub.expiresAt.has_value(), "expired subscription should expose expiresAt");
  require(*expiredSub.expiresAt < current, "expired subscription expiresAt should be in the past");

  const auto pastDueSub = otomarket::license::evaluatePack(snapshot, "pack-sub-past-due", current);
  require(pastDueSub.entitled, "past_due subscription pack should match");
  requireEqual(pastDueSub.source, SnapshotSource::Subscription, "past_due subscription source mismatch");

  const auto activeTrial = otomarket::license::evaluatePack(snapshot, "pack-trial-active", current);
  require(activeTrial.entitled, "active trial pack should match");
  requireEqual(activeTrial.source, SnapshotSource::Free, "trial access source should use Free");
  require(activeTrial.expiresAt.has_value(), "active trial should expose expiresAt");
  require(*activeTrial.expiresAt > current, "active trial expiresAt should be in the future");

  const auto expiredTrial = otomarket::license::evaluatePack(snapshot, "pack-trial-expired", current);
  require(expiredTrial.entitled, "expired trial pack should still report a signed match");
  require(expiredTrial.expiresAt.has_value(), "expired trial should expose expiresAt");
  require(*expiredTrial.expiresAt < current, "expired trial expiresAt should be in the past");

  const auto staleInsideGrace = otomarket::license::evaluatePack(
    snapshot,
    "pack-buyout",
    makeTime(2026, 6, 4, 12, 6, 0)
  );
  require(!staleInsideGrace.snapshotFresh, "snapshot should be stale after exp");
  require(staleInsideGrace.withinOfflineGrace, "snapshot should remain inside offline grace");

  const auto afterGrace = otomarket::license::evaluatePack(
    snapshot,
    "pack-buyout",
    makeTime(2026, 6, 12, 12, 0, 0)
  );
  require(!afterGrace.withinOfflineGrace, "snapshot should report offline grace expiry");
}

void rejectsUnknownLicenseSnapshotSchemaVersion() {
  const KeyPair keyPair = generateKeyPair();
  const std::string token = snapshotToken(keyPair, snapshotPayloadJson("99"));

  const auto result = otomarket::license::verifyLicenseSnapshot(
    token,
    keyPair.publicKeyPem
  );

  require(!result.ok, "unknown snapshot schemaVersion should fail");
  requireEqual(result.error, ErrorCode::InvalidSignature, "unknown schema error mismatch");
}

void activationUiShowsLicensedDetails() {
  otomarket::license::LicenseInfo info;
  info.maxActivations = 2;
  info.expiresAt = makeTime(2026, 6, 18, 12, 0, 0);

  otomarket::license::LicenseActivationViewInput input;
  input.state = LicenseState::Licensed;
  input.license = info;
  input.seatsUsed = 1;
  input.hasLicenseKey = true;

  const auto view = otomarket::license::buildLicenseActivationView(input);

  requireEqual(view.statusText, std::string("Licensed"), "licensed status text mismatch");
  require(view.detailsText.find("Seats: 1/2") != std::string::npos, "seats detail mismatch");
  require(view.detailsText.find("Expires: 2026-06-18 12:00 UTC") != std::string::npos, "expires detail mismatch");
  require(!view.canActivate, "licensed view should not activate again");
  require(view.canDeactivate, "licensed view should allow deactivate");
}

void activationUiMapsErrorsToMessages() {
  const auto strings = otomarket::license::englishLicenseActivationStrings();

  requireEqual(
    otomarket::license::licenseActivationErrorMessage(ErrorCode::SeatLimit, strings),
    strings.errorSeatLimit,
    "SEAT_LIMIT message mismatch"
  );
  requireEqual(
    otomarket::license::licenseActivationErrorMessage(ErrorCode::Expired, strings),
    strings.errorExpired,
    "EXPIRED message mismatch"
  );
  requireEqual(
    otomarket::license::licenseActivationErrorMessage(ErrorCode::Revoked, strings),
    strings.errorRevoked,
    "REVOKED message mismatch"
  );
  requireEqual(
    otomarket::license::licenseActivationErrorMessage(ErrorCode::NotFound, strings),
    strings.errorNotFound,
    "NOT_FOUND message mismatch"
  );
  requireEqual(
    otomarket::license::licenseActivationErrorMessage(ErrorCode::ProductMismatch, strings),
    strings.errorProductMismatch,
    "PRODUCT_MISMATCH message mismatch"
  );
  requireEqual(
    otomarket::license::licenseActivationErrorMessage(ErrorCode::NetworkError, strings),
    strings.errorNetwork,
    "NETWORK_ERROR message mismatch"
  );
  requireEqual(
    otomarket::license::licenseActivationErrorMessage(ErrorCode::RequestRejected, strings),
    strings.errorRequestRejected,
    "REQUEST_REJECTED message mismatch"
  );
}

void activationUiLocaleDefaultsAreLocalized() {
  const auto englishFields =
    activationStringFields(otomarket::license::englishLicenseActivationStrings());
  const auto japaneseFields =
    activationStringFields(otomarket::license::japaneseLicenseActivationStrings());

  requireEqual(
    englishFields[4].second,
    std::string("Start free trial"),
    "English trial button mismatch"
  );
  requireEqual(
    japaneseFields[4].second,
    std::string("無料でためす"),
    "Japanese trial button mismatch"
  );

  const std::array<LocalizedActivationStringsCase, 7> locales = {{
    {"de", otomarket::license::germanLicenseActivationStrings(), "Lizenzschlüssel"},
    {"es", otomarket::license::spanishLicenseActivationStrings(), "Clave de licencia"},
    {"pt-BR", otomarket::license::brazilianPortugueseLicenseActivationStrings(), "Chave de licença"},
    {"zh-CN", otomarket::license::simplifiedChineseLicenseActivationStrings(), "许可证密钥"},
    {"fr", otomarket::license::frenchLicenseActivationStrings(), "Clé de licence"},
    {"ru", otomarket::license::russianLicenseActivationStrings(), "Лицензионный ключ"},
    {"ko", otomarket::license::koreanLicenseActivationStrings(), "라이선스 키"},
  }};

  for (const auto& locale : locales) {
    requireEqual(
      locale.strings.licenseKeyLabel,
      locale.licenseKeyLabel,
      locale.locale + " license key label mismatch"
    );

    const auto fields = activationStringFields(locale.strings);
    for (size_t index = 0; index < fields.size(); ++index) {
      require(
        !fields[index].second.empty(),
        locale.locale + " " + fields[index].first + " should not be empty"
      );
      require(
        fields[index].second != englishFields[index].second,
        locale.locale + " " + fields[index].first + " should not reuse English default"
      );
    }
  }
}

void activationUiButtonStatesAreStable() {
  otomarket::license::LicenseActivationViewInput input;
  input.state = LicenseState::Unlicensed;

  auto view = otomarket::license::buildLicenseActivationView(input);
  require(!view.canActivate, "empty key should not activate");
  require(!view.canDeactivate, "unlicensed view should not deactivate");

  input.hasLicenseKey = true;
  view = otomarket::license::buildLicenseActivationView(input);
  require(view.canActivate, "entered key should activate");
  require(!view.canDeactivate, "missing cache should not deactivate");

  input.busy = true;
  input.busyMessage = "Working";
  view = otomarket::license::buildLicenseActivationView(input);
  requireEqual(view.statusText, std::string("Working"), "busy text mismatch");
  require(!view.canActivate, "busy view should disable activate");

  input.busy = false;
  input.state = LicenseState::LicensedOfflineGrace;
  input.license = otomarket::license::LicenseInfo{};
  view = otomarket::license::buildLicenseActivationView(input);
  require(!view.canActivate, "offline grace view should not activate");
  require(view.canDeactivate, "cached offline grace view should deactivate");
  require(view.detailsText.find("offline retry grace") != std::string::npos, "offline grace detail mismatch");
}

void versionMacrosMatchProjectVersion() {
  requireEqual(OTOMARKET_LICENSE_SDK_VERSION_MAJOR, 1, "major version mismatch");
  requireEqual(OTOMARKET_LICENSE_SDK_VERSION_MINOR, 1, "minor version mismatch");
  requireEqual(OTOMARKET_LICENSE_SDK_VERSION_PATCH, 0, "patch version mismatch");
  requireEqual(
    std::string(OTOMARKET_LICENSE_SDK_VERSION_STRING),
    std::string("1.1.0"),
    "version string mismatch"
  );
}

void runTest(const std::string& name, const std::function<void()>& test) {
  test();
  std::cout << "[ok] " << name << "\n";
}

} // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
    {"verifiesGeneratedEd25519Token", verifiesGeneratedEd25519Token},
    {"parsesTrialTokenFlag", parsesTrialTokenFlag},
    {"verifiesPublicKeyFormatsNormalizeToSameEd25519Key", verifiesPublicKeyFormatsNormalizeToSameEd25519Key},
    {"rejectsTamperedToken", rejectsTamperedToken},
    {"rejectsExpiredToken", rejectsExpiredToken},
    {"rejectsTokenIssuedTooFarInFuture", rejectsTokenIssuedTooFarInFuture},
    {"capsOfflineTrustAtMaxOffline", capsOfflineTrustAtMaxOffline},
    {"verifiesKidTokenWithKeyset", verifiesKidTokenWithKeyset},
    {"legacyTokenWithoutKidUsesPublicKeyPem", legacyTokenWithoutKidUsesPublicKeyPem},
    {"rejectsKidMissingFromKeyset", rejectsKidMissingFromKeyset},
    {"clientUsesConfigKeysetForCachedKidToken", clientUsesConfigKeysetForCachedKidToken},
    {"clientUsesConfigKeysetForActivationToken", clientUsesConfigKeysetForActivationToken},
    {"clientFetchesKeysetWithGetForActivationKidToken", clientFetchesKeysetWithGetForActivationKidToken},
    {"keysUnsupportedStillAllowsLegacyActivationToken", keysUnsupportedStillAllowsLegacyActivationToken},
    {"cachedKeysetKeepsLicenseWhenRefreshGetFails", cachedKeysetKeepsLicenseWhenRefreshGetFails},
    {"fileKeysetCacheRestoresForNewClient", fileKeysetCacheRestoresForNewClient},
    {"cachesRoundTripOffline", cachesRoundTripOffline},
    {"verifyAfterBoundaryTriggersOnlineVerify", verifyAfterBoundaryTriggersOnlineVerify},
    {"offlineGraceAllowsTransientVerifyFailure", offlineGraceAllowsTransientVerifyFailure},
    {"offlineGraceExpiresAfterBoundary", offlineGraceExpiresAfterBoundary},
    {"machineIdDerivationIsStable", machineIdDerivationIsStable},
    {"activateVerifyDeactivateFlowUsesMockHttp", activateVerifyDeactivateFlowUsesMockHttp},
    {"activateOmitsEmptyMachineName", activateOmitsEmptyMachineName},
    {"activateSendsProvidedMachineName", activateSendsProvidedMachineName},
    {"http400ApiErrorClassifiesAsRequestRejected", http400ApiErrorClassifiesAsRequestRejected},
    {"http409SeatLimitClassifiesAsSeatLimit", http409SeatLimitClassifiesAsSeatLimit},
    {"startTrialCachesTrialToken", startTrialCachesTrialToken},
    {"startTrialReturnsServerTrialError", startTrialReturnsServerTrialError},
    {"verifiesGeneratedLicenseSnapshot", verifiesGeneratedLicenseSnapshot},
    {"parsesLicenseSnapshotPacks", parsesLicenseSnapshotPacks},
    {"parsesLicenseSnapshotWithoutPacks", parsesLicenseSnapshotWithoutPacks},
    {"verifiesPackContentHashes", verifiesPackContentHashes},
    {"rejectsTamperedLicenseSnapshot", rejectsTamperedLicenseSnapshot},
    {"rejectsLicenseSnapshotSignedByAnotherKey", rejectsLicenseSnapshotSignedByAnotherKey},
    {"verifiesLicenseSnapshotKidKeyset", verifiesLicenseSnapshotKidKeyset},
    {"evaluatePackUsesSnapshotFacts", evaluatePackUsesSnapshotFacts},
    {"rejectsUnknownLicenseSnapshotSchemaVersion", rejectsUnknownLicenseSnapshotSchemaVersion},
    {"activationUiShowsLicensedDetails", activationUiShowsLicensedDetails},
    {"activationUiMapsErrorsToMessages", activationUiMapsErrorsToMessages},
    {"activationUiLocaleDefaultsAreLocalized", activationUiLocaleDefaultsAreLocalized},
    {"activationUiButtonStatesAreStable", activationUiButtonStatesAreStable},
    {"versionMacrosMatchProjectVersion", versionMacrosMatchProjectVersion},
  };

  int failures = 0;

  for (const auto& test : tests) {
    try {
      runTest(test.first, test.second);
    } catch (const std::exception& exception) {
      ++failures;
      std::cerr << "[failed] " << test.first << ": " << exception.what() << "\n";
    }
  }

  return failures == 0 ? 0 : 1;
}
