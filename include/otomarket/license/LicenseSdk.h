#pragma once

#include <otomarket/license/Version.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace otomarket::license {

using Clock = std::function<std::chrono::system_clock::time_point()>;

enum class ErrorCode {
  None,
  SeatLimit,
  Expired,
  Revoked,
  NotFound,
  ProductMismatch,
  TrialNotAvailable,
  TrialExpired,
  NetworkError,
  InvalidResponse,
  InvalidSignature,
  InvalidCache,
  CacheIoError,
  CacheMissing,
  MachineMismatch,
  ConfigError,
  RequestRejected,
};

enum class LicenseState {
  Unknown,
  Unlicensed,
  Licensed,
  LicensedOfflineGrace,
  VerifyDue,
  Expired,
  Revoked,
  NotFound,
  ProductMismatch,
  SeatLimit,
  InvalidCache,
  NetworkUnavailable,
  Error,
};

std::string toString(ErrorCode code);
std::string toString(LicenseState state);

struct HttpHeader {
  std::string name;
  std::string value;
};

struct HttpResponse {
  int statusCode = 0;
  std::string body;
};

class HttpTransport {
public:
  virtual ~HttpTransport() = default;

  virtual HttpResponse postJson(
    const std::string& url,
    const std::string& body,
    const std::vector<HttpHeader>& headers
  ) = 0;

  virtual HttpResponse getJson(
    const std::string& url,
    const std::vector<HttpHeader>& headers
  ) {
    (void)url;
    (void)headers;
    return HttpResponse{};
  }
};

struct LicenseInfo {
  std::string licenseId;
  std::string productId;
  std::string buyerId;
  std::string machineId;
  int maxActivations = 0;
  std::chrono::system_clock::time_point issuedAt{};
  std::optional<std::chrono::system_clock::time_point> expiresAt;
  std::chrono::system_clock::time_point verifyAfter{};
  bool isTrial = false;
};

struct ActivateResult {
  bool ok = false;
  ErrorCode error = ErrorCode::None;
  std::string errorMessage;
  std::string licenseToken;
  int seatsUsed = 0;
  int maxActivations = 0;
  std::optional<std::chrono::system_clock::time_point> expiresAt;
};

struct DeactivateResult {
  bool ok = false;
  ErrorCode error = ErrorCode::None;
  std::string errorMessage;
  int seatsUsed = 0;
};

struct VerifyTokenResult {
  bool ok = false;
  ErrorCode error = ErrorCode::None;
  std::string errorMessage;
  std::optional<LicenseInfo> license;
};

struct Config {
  std::string baseUrl;
  std::string keysUrl;
  std::string publicKeyPem;
  std::filesystem::path cachePath;
  std::shared_ptr<HttpTransport> http;
  std::string expectedProductId;
  std::string machineId;
  std::string machineFingerprint;
  std::chrono::seconds verifyRetryGrace = std::chrono::hours(72);
  Clock clock;
  std::chrono::seconds maxOffline = std::chrono::hours(24 * 30);
  std::map<std::string, std::string> keyset;
  std::chrono::seconds keysetTtl = std::chrono::hours(24);
};

std::string defaultMachineFingerprint();
std::string deriveMachineId(const std::string& productId, const std::string& machineFingerprint);
std::filesystem::path defaultCachePath(const std::string& appName);

VerifyTokenResult verifyLicenseToken(
  const std::string& token,
  const std::string& publicKeyPem,
  const std::string& expectedProductId = {},
  const std::string& expectedMachineId = {},
  std::optional<std::chrono::system_clock::time_point> now = std::nullopt,
  const std::map<std::string, std::string>& keyset = {}
);

class Client {
public:
  explicit Client(Config config);

  ActivateResult otoActivate(
    const std::string& productId,
    const std::string& licenseKey,
    const std::string& machineName = {}
  );

  ActivateResult otoStartTrial(const std::string& productId);

  bool otoIsLicensed();
  DeactivateResult otoDeactivate();

  LicenseState state() const;
  ErrorCode lastError() const;
  std::optional<LicenseInfo> cachedLicense() const;
  std::string machineIdForProduct(const std::string& productId) const;

private:
  struct CacheRecord;

  std::chrono::system_clock::time_point now() const;
  std::string endpointUrl(const std::string& action) const;
  std::optional<CacheRecord> readCache(ErrorCode& error, std::string& message) const;
  bool writeCache(const CacheRecord& cache, std::string& message) const;
  bool removeCache(std::string& message) const;
  bool tryOnlineVerify(const CacheRecord& cache, LicenseInfo& license);
  void loadKeysetCache();
  void refreshKeyset();
  void refreshKeysetIfDue();
  VerifyTokenResult verifyTokenWithKeysetRefresh(
    const std::string& token,
    const std::string& expectedProductId,
    const std::string& expectedMachineId,
    std::chrono::system_clock::time_point currentTime
  );
  void setState(LicenseState state, ErrorCode error = ErrorCode::None);

  Config config_;
  LicenseState state_ = LicenseState::Unknown;
  ErrorCode lastError_ = ErrorCode::None;
  std::optional<LicenseInfo> cachedLicense_;
  std::optional<std::chrono::system_clock::time_point> keysetFetchedAt_;
};

} // namespace otomarket::license
