#pragma once

#include <otomarket/license/LicenseSdk.h>

#include <chrono>
#include <optional>
#include <string>

namespace otomarket::license {

struct LicenseActivationStrings {
  std::string licenseKeyLabel = "License key";
  std::string licenseKeyPlaceholder = "Enter license key";
  std::string activateButton = "Activate";
  std::string deactivateButton = "Deactivate";
  std::string trialButton = "Start free trial";
  std::string checkingStatus = "Checking license...";
  std::string activatingStatus = "Activating...";
  std::string deactivatingStatus = "Deactivating...";
  std::string unknownStatus = "License status unknown";
  std::string unlicensedStatus = "Not licensed";
  std::string licensedStatus = "Licensed";
  std::string offlineGraceStatus = "Licensed (offline grace)";
  std::string verifyDueStatus = "Online verification required";
  std::string expiredStatus = "Expired";
  std::string revokedStatus = "Revoked";
  std::string notFoundStatus = "License not found";
  std::string productMismatchStatus = "Product mismatch";
  std::string seatLimitStatus = "Activation limit reached";
  std::string invalidCacheStatus = "Saved license is invalid";
  std::string networkUnavailableStatus = "License server unavailable";
  std::string errorStatus = "License error";
  std::string seatsLabel = "Seats";
  std::string maxActivationsLabel = "Max activations";
  std::string expiresAtLabel = "Expires";
  std::string noExpirationText = "No expiration";
  std::string offlineGraceDetail = "Using the saved license during the offline retry grace period.";
  std::string errorSeatLimit = "This license has reached its activation limit.";
  std::string errorExpired = "This license has expired.";
  std::string errorRevoked = "This license has been revoked.";
  std::string errorNotFound = "License key was not found.";
  std::string errorProductMismatch = "This license is for a different product.";
  std::string errorNetwork = "Could not reach the license server. Check your connection and try again.";
  std::string errorRequestRejected = "The license server rejected the request.";
  std::string errorInvalidResponse = "The license server returned an invalid response.";
  std::string errorInvalidSignature = "The saved license signature is invalid.";
  std::string errorInvalidCache = "The saved license file is invalid.";
  std::string errorCacheIo = "Could not read or write the saved license file.";
  std::string errorCacheMissing = "No saved license was found.";
  std::string errorMachineMismatch = "This license was activated on a different machine.";
  std::string errorConfig = "License SDK configuration is incomplete.";
  std::string errorFallback = "License operation failed.";
};

LicenseActivationStrings englishLicenseActivationStrings();
LicenseActivationStrings japaneseLicenseActivationStrings();
// Machine-translated defaults, pending native review.
// 機械翻訳・ネイティブ校正待ち。
LicenseActivationStrings germanLicenseActivationStrings();
LicenseActivationStrings spanishLicenseActivationStrings();
LicenseActivationStrings brazilianPortugueseLicenseActivationStrings();
LicenseActivationStrings simplifiedChineseLicenseActivationStrings();
LicenseActivationStrings frenchLicenseActivationStrings();
LicenseActivationStrings russianLicenseActivationStrings();
LicenseActivationStrings koreanLicenseActivationStrings();

struct LicenseActivationViewInput {
  LicenseState state = LicenseState::Unknown;
  ErrorCode error = ErrorCode::None;
  std::string errorMessage;
  std::optional<LicenseInfo> license;
  std::optional<int> seatsUsed;
  std::optional<int> maxActivations;
  std::optional<std::chrono::system_clock::time_point> expiresAt;
  bool busy = false;
  bool hasLicenseKey = false;
  std::string busyMessage;
};

struct LicenseActivationViewModel {
  std::string statusText;
  std::string detailsText;
  std::string errorText;
  bool canActivate = false;
  bool canDeactivate = false;
};

std::string formatLicenseActivationDate(std::chrono::system_clock::time_point value);

std::string licenseActivationErrorMessage(
  ErrorCode code,
  const LicenseActivationStrings& strings = englishLicenseActivationStrings(),
  const std::string& fallbackMessage = {}
);

LicenseActivationViewModel buildLicenseActivationView(
  const LicenseActivationViewInput& input,
  const LicenseActivationStrings& strings = englishLicenseActivationStrings()
);

} // namespace otomarket::license
