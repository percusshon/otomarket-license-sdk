#include <otomarket/license/LicenseSdk.h>
#include <otomarket/license/LicenseActivationUi.h>
#include <otomarket/license/Version.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
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

namespace {

constexpr const char* kNowIso = "2026-06-04T12:00:00.000Z";
constexpr const char* kPastIso = "2026-06-04T11:00:00.000Z";
constexpr const char* kOlderPastIso = "2026-06-04T09:00:00.000Z";
constexpr const char* kFutureIso = "2026-06-18T12:00:00.000Z";

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
  std::function<HttpResponse(const HttpRequest&)> handler;

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

std::array<std::pair<std::string, std::string>, 39> activationStringFields(
  const otomarket::license::LicenseActivationStrings& strings
) {
  return {{
    {"licenseKeyLabel", strings.licenseKeyLabel},
    {"licenseKeyPlaceholder", strings.licenseKeyPlaceholder},
    {"activateButton", strings.activateButton},
    {"deactivateButton", strings.deactivateButton},
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
  const std::string& expiresAt = "null"
) {
  return std::string("{") +
    "\"licenseId\":\"license-1\"," +
    "\"productId\":" + jsonString(productId) + "," +
    "\"buyerId\":\"buyer-1\"," +
    "\"machineId\":" + jsonString(machineId) + "," +
    "\"maxActivations\":2," +
    "\"issuedAt\":\"2026-06-04T12:00:00.000Z\"," +
    "\"expiresAt\":" + expiresAt + "," +
    "\"verifyAfter\":" + jsonString(verifyAfter) +
    "}";
}

std::string signToken(const KeyPair& keyPair, const std::string& payload) {
  const std::string header = R"({"alg":"EdDSA","typ":"JWT"})";
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
  const std::string& expiresAt = "null"
) {
  return signToken(keyPair, payloadJson(productId, machineId, verifyAfter, expiresAt));
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
}

void activationUiLocaleDefaultsAreLocalized() {
  const auto englishFields =
    activationStringFields(otomarket::license::englishLicenseActivationStrings());

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
  requireEqual(OTOMARKET_LICENSE_SDK_VERSION_MAJOR, 0, "major version mismatch");
  requireEqual(OTOMARKET_LICENSE_SDK_VERSION_MINOR, 1, "minor version mismatch");
  requireEqual(OTOMARKET_LICENSE_SDK_VERSION_PATCH, 0, "patch version mismatch");
  requireEqual(
    std::string(OTOMARKET_LICENSE_SDK_VERSION_STRING),
    std::string("0.1.0"),
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
    {"rejectsTamperedToken", rejectsTamperedToken},
    {"rejectsExpiredToken", rejectsExpiredToken},
    {"cachesRoundTripOffline", cachesRoundTripOffline},
    {"verifyAfterBoundaryTriggersOnlineVerify", verifyAfterBoundaryTriggersOnlineVerify},
    {"offlineGraceAllowsTransientVerifyFailure", offlineGraceAllowsTransientVerifyFailure},
    {"offlineGraceExpiresAfterBoundary", offlineGraceExpiresAfterBoundary},
    {"machineIdDerivationIsStable", machineIdDerivationIsStable},
    {"activateVerifyDeactivateFlowUsesMockHttp", activateVerifyDeactivateFlowUsesMockHttp},
    {"activateOmitsEmptyMachineName", activateOmitsEmptyMachineName},
    {"activateSendsProvidedMachineName", activateSendsProvidedMachineName},
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
