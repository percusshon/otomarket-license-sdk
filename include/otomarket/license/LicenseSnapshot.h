#pragma once

#include <otomarket/license/LicenseSdk.h>

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace otomarket::license {

enum class SnapshotSource {
  Buyout,
  Subscription,
  Free,
};

enum class SubscriptionStatus {
  None,
  Active,
  PastDue,
};

struct SnapshotPack {
  std::string packId;
  std::optional<std::string> contentHash;
};

struct SnapshotEntitlement {
  std::string productId;
  std::optional<std::string> packId;
  std::vector<std::string> packIds;
  std::vector<SnapshotPack> packs;
  std::optional<std::string> saleMode;
  SnapshotSource source = SnapshotSource::Buyout;
  std::optional<std::chrono::system_clock::time_point> expiresAt;
  SubscriptionStatus subscriptionStatus = SubscriptionStatus::None;
  std::vector<std::string> licenseKeys;
  std::chrono::system_clock::time_point acquiredAt{};
};

struct SnapshotTrial {
  std::string productId;
  std::optional<std::string> packId;
  std::vector<std::string> packIds;
  std::vector<SnapshotPack> packs;
  std::chrono::system_clock::time_point startedAt{};
  std::chrono::system_clock::time_point expiresAt{};
  std::string status;
};

struct LicenseSnapshot {
  int schemaVersion = 0;
  std::string issuer;
  std::string sub;
  std::chrono::system_clock::time_point iat{};
  std::chrono::system_clock::time_point exp{};
  int offlineGracePeriodDays = 0;
  std::chrono::system_clock::time_point offlineGraceExpiresAt{};
  std::vector<SnapshotEntitlement> entitlements;
  std::vector<SnapshotTrial> trials;
};

enum class PackContentResult {
  Match,
  Mismatch,
  Unknown,
};

struct SnapshotVerifyResult {
  bool ok = false;
  ErrorCode error = ErrorCode::None;
  std::string message;
  std::optional<LicenseSnapshot> snapshot;
};

SnapshotVerifyResult verifyLicenseSnapshot(
  const std::string& jws,
  const std::string& publicKeyPem
);

SnapshotVerifyResult verifyLicenseSnapshot(
  const std::string& jws,
  const std::map<std::string, std::string>& keysetByKid
);

struct PackAccess {
  bool entitled = false;
  SnapshotSource source = SnapshotSource::Buyout;
  std::optional<std::chrono::system_clock::time_point> expiresAt;
  bool snapshotFresh = false;
  bool withinOfflineGrace = false;
  std::optional<std::string> matchedProductId;
};

PackAccess evaluatePack(
  const LicenseSnapshot& snap,
  const std::string& packId,
  std::chrono::system_clock::time_point now
);

PackContentResult verifyPackContent(
  const LicenseSnapshot& snapshot,
  const std::string& packId,
  const std::string& fileSha256Hex
);

} // namespace otomarket::license
