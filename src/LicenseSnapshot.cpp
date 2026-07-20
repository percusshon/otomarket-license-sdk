#include <otomarket/license/LicenseSnapshot.h>

#include "LicenseInternal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace otomarket::license {
namespace {

using namespace detail;

constexpr int kSupportedSchemaVersion = 2;
constexpr const char* kMissingKidErrorMessage = "License snapshot kid was not found in keyset.";

SnapshotVerifyResult failure(ErrorCode error, std::string message) {
  SnapshotVerifyResult result;
  result.error = error;
  result.message = std::move(message);
  return result;
}

struct DecodedSnapshotJws {
  std::vector<std::string> parts;
  JsonValue header;
  std::string payloadJson;
  std::vector<unsigned char> signature;
};

std::optional<DecodedSnapshotJws> decodeSnapshotJws(
  const std::string& jws,
  SnapshotVerifyResult& result
) {
  if (jws.size() > kMaxTokenEncodedSize) {
    result = failure(
      ErrorCode::InvalidSignature,
      "License snapshot exceeds the maximum supported size."
    );
    return std::nullopt;
  }

  DecodedSnapshotJws decoded;
  decoded.parts = splitToken(jws);

  if (!tokenEncodedSizesAreValid(jws, decoded.parts) ||
      decoded.parts[0].empty() ||
      decoded.parts[1].empty() ||
      decoded.parts[2].empty()) {
    result = failure(
      ErrorCode::InvalidSignature,
      "License snapshot must use header.payload.signature format."
    );
    return std::nullopt;
  }

  try {
    const std::vector<unsigned char> headerBytes = base64Decode(decoded.parts[0]);
    const std::vector<unsigned char> payloadBytes = base64Decode(decoded.parts[1]);
    decoded.signature = base64Decode(decoded.parts[2]);
    decoded.header = JsonParser(std::string(headerBytes.begin(), headerBytes.end())).parse();
    decoded.payloadJson = std::string(payloadBytes.begin(), payloadBytes.end());
  } catch (const std::exception& exception) {
    result = failure(ErrorCode::InvalidSignature, exception.what());
    return std::nullopt;
  }

  const auto alg = stringField(decoded.header, "alg");
  const auto typ = stringField(decoded.header, "typ");

  if (!alg || *alg != "EdDSA" || !typ || *typ != "JWT") {
    result = failure(
      ErrorCode::InvalidSignature,
      "License snapshot header is not EdDSA/JWT."
    );
    return std::nullopt;
  }

  if (const JsonValue* kidField = objectField(decoded.header, "kid");
      kidField != nullptr && kidField->type != JsonValue::Type::String) {
    result = failure(
      ErrorCode::InvalidSignature,
      "License snapshot header kid must be a string."
    );
    return std::nullopt;
  }

  return decoded;
}

std::optional<std::string> snapshotKid(const JsonValue& header) {
  const JsonValue* kidField = objectField(header, "kid");
  if (kidField == nullptr) {
    return std::nullopt;
  }
  return kidField->stringValue;
}

bool verifyDecodedJws(
  const DecodedSnapshotJws& decoded,
  const std::string& publicKeyPem,
  SnapshotVerifyResult& result
) {
  std::array<unsigned char, 32> publicKey{};
  try {
    publicKey = parsePublicKey(publicKeyPem);
  } catch (const std::exception& exception) {
    result = failure(ErrorCode::InvalidSignature, exception.what());
    return false;
  }

  const std::string signingInput = decoded.parts[0] + "." + decoded.parts[1];
  if (!verifyEd25519Detached(signingInput, decoded.signature, publicKey)) {
    result = failure(
      ErrorCode::InvalidSignature,
      "License snapshot signature did not verify."
    );
    return false;
  }

  return true;
}

const JsonValue* requiredField(
  const JsonValue& value,
  const std::string& key,
  std::string& message
) {
  const JsonValue* field = objectField(value, key);
  if (field == nullptr) {
    message = "License snapshot is missing required field: " + key + ".";
    return nullptr;
  }
  return field;
}

std::optional<std::string> requiredString(
  const JsonValue& value,
  const std::string& key,
  std::string& message
) {
  const JsonValue* field = requiredField(value, key, message);
  if (field == nullptr) {
    return std::nullopt;
  }
  if (field->type != JsonValue::Type::String) {
    message = "License snapshot field must be a string: " + key + ".";
    return std::nullopt;
  }
  return field->stringValue;
}

std::optional<std::string> optionalNullableString(
  const JsonValue& value,
  const std::string& key,
  bool required,
  std::string& message
) {
  const JsonValue* field = required ? requiredField(value, key, message) : objectField(value, key);
  if (field == nullptr) {
    return std::nullopt;
  }
  if (field->type == JsonValue::Type::Null) {
    return std::nullopt;
  }
  if (field->type != JsonValue::Type::String) {
    message = "License snapshot field must be a string or null: " + key + ".";
    return std::nullopt;
  }
  return field->stringValue;
}

std::optional<long long> requiredInteger(
  const JsonValue& value,
  const std::string& key,
  std::string& message
) {
  const JsonValue* field = requiredField(value, key, message);
  if (field == nullptr) {
    return std::nullopt;
  }
  if (field->type != JsonValue::Type::Number) {
    message = "License snapshot field must be an integer: " + key + ".";
    return std::nullopt;
  }

  const double number = field->numberValue;
  if (number < static_cast<double>(std::numeric_limits<long long>::min()) ||
      number > static_cast<double>(std::numeric_limits<long long>::max())) {
    message = "License snapshot integer field is out of range: " + key + ".";
    return std::nullopt;
  }

  const auto integer = static_cast<long long>(number);
  if (static_cast<double>(integer) != number) {
    message = "License snapshot field must be an integer: " + key + ".";
    return std::nullopt;
  }

  return integer;
}

std::optional<int> requiredInt(
  const JsonValue& value,
  const std::string& key,
  std::string& message
) {
  const auto integer = requiredInteger(value, key, message);
  if (!integer) {
    return std::nullopt;
  }
  if (*integer < std::numeric_limits<int>::min() ||
      *integer > std::numeric_limits<int>::max()) {
    message = "License snapshot integer field is out of range: " + key + ".";
    return std::nullopt;
  }
  return static_cast<int>(*integer);
}

std::optional<std::chrono::system_clock::time_point> requiredEpochSeconds(
  const JsonValue& value,
  const std::string& key,
  std::string& message
) {
  const auto seconds = requiredInteger(value, key, message);
  if (!seconds) {
    return std::nullopt;
  }
  return std::chrono::system_clock::from_time_t(static_cast<std::time_t>(*seconds));
}

std::optional<std::chrono::system_clock::time_point> requiredIsoTime(
  const JsonValue& value,
  const std::string& key,
  std::string& message
) {
  const auto stringValue = requiredString(value, key, message);
  if (!stringValue) {
    return std::nullopt;
  }

  try {
    return parseIso8601(*stringValue);
  } catch (const std::exception& exception) {
    message = exception.what();
    return std::nullopt;
  }
}

std::optional<std::chrono::system_clock::time_point> optionalNullableIsoTime(
  const JsonValue& value,
  const std::string& key,
  bool required,
  std::string& message
) {
  const auto stringValue = optionalNullableString(value, key, required, message);
  if (!message.empty() || !stringValue) {
    return std::nullopt;
  }

  try {
    return parseIso8601(*stringValue);
  } catch (const std::exception& exception) {
    message = exception.what();
    return std::nullopt;
  }
}

std::optional<std::vector<std::string>> requiredStringArray(
  const JsonValue& value,
  const std::string& key,
  std::string& message
) {
  const JsonValue* field = requiredField(value, key, message);
  if (field == nullptr) {
    return std::nullopt;
  }
  if (field->type != JsonValue::Type::Array) {
    message = "License snapshot field must be an array: " + key + ".";
    return std::nullopt;
  }

  std::vector<std::string> strings;
  strings.reserve(field->arrayValue.size());
  for (const auto& item : field->arrayValue) {
    if (item.type != JsonValue::Type::String) {
      message = "License snapshot array must contain only strings: " + key + ".";
      return std::nullopt;
    }
    strings.push_back(item.stringValue);
  }
  return strings;
}

std::optional<std::vector<SnapshotPack>> optionalPackArray(
  const JsonValue& value,
  const std::string& key,
  std::string& message
) {
  const JsonValue* field = objectField(value, key);
  if (field == nullptr) {
    return std::vector<SnapshotPack>{};
  }
  if (field->type != JsonValue::Type::Array) {
    message = "License snapshot field must be an array: " + key + ".";
    return std::nullopt;
  }

  std::vector<SnapshotPack> packs;
  packs.reserve(field->arrayValue.size());
  for (const auto& item : field->arrayValue) {
    if (item.type != JsonValue::Type::Object) {
      message = "License snapshot pack must be an object.";
      return std::nullopt;
    }

    auto packId = requiredString(item, "packId", message);
    auto contentHash = optionalNullableString(item, "contentHash", true, message);
    if (!packId || !message.empty()) {
      return std::nullopt;
    }

    SnapshotPack pack;
    pack.packId = std::move(*packId);
    pack.contentHash = std::move(contentHash);
    packs.push_back(std::move(pack));
  }

  return packs;
}

std::optional<SnapshotSource> parseSnapshotSource(const std::string& value) {
  if (value == "buyout") {
    return SnapshotSource::Buyout;
  }
  if (value == "subscription") {
    return SnapshotSource::Subscription;
  }
  if (value == "free") {
    return SnapshotSource::Free;
  }
  if (value == "membership") {
    return SnapshotSource::Membership;
  }
  return SnapshotSource::Unknown;
}

std::optional<SubscriptionStatus> parseSubscriptionStatus(
  const JsonValue& entitlement,
  std::string& message
) {
  const JsonValue* field = requiredField(entitlement, "subscriptionStatus", message);
  if (field == nullptr) {
    return std::nullopt;
  }
  if (field->type == JsonValue::Type::Null) {
    return SubscriptionStatus::None;
  }
  if (field->type != JsonValue::Type::String) {
    message = "License snapshot subscriptionStatus must be a string or null.";
    return std::nullopt;
  }
  if (field->stringValue == "active") {
    return SubscriptionStatus::Active;
  }
  if (field->stringValue == "past_due") {
    return SubscriptionStatus::PastDue;
  }
  message = "License snapshot subscriptionStatus is unknown.";
  return std::nullopt;
}

std::optional<SnapshotEntitlement> parseEntitlement(
  const JsonValue& value,
  std::string& message
) {
  if (value.type != JsonValue::Type::Object) {
    message = "License snapshot entitlement must be an object.";
    return std::nullopt;
  }

  SnapshotEntitlement entitlement;
  auto productId = requiredString(value, "productId", message);
  auto packId = optionalNullableString(value, "packId", true, message);
  if (!message.empty()) {
    return std::nullopt;
  }
  auto packIds = requiredStringArray(value, "packIds", message);
  auto packs = optionalPackArray(value, "packs", message);
  auto saleMode = optionalNullableString(value, "saleMode", true, message);
  if (!message.empty()) {
    return std::nullopt;
  }
  auto sourceString = requiredString(value, "source", message);
  auto expiresAt = optionalNullableIsoTime(value, "expiresAt", true, message);
  if (!message.empty()) {
    return std::nullopt;
  }
  auto subscriptionStatus = parseSubscriptionStatus(value, message);
  auto licenseKeys = requiredStringArray(value, "licenseKeys", message);
  auto acquiredAt = requiredIsoTime(value, "acquiredAt", message);

  if (!productId || !packIds || !sourceString || !subscriptionStatus ||
      !licenseKeys || !acquiredAt || !packs) {
    return std::nullopt;
  }

  const auto source = parseSnapshotSource(*sourceString);

  entitlement.productId = *productId;
  entitlement.packId = std::move(packId);
  entitlement.packIds = std::move(*packIds);
  entitlement.packs = std::move(*packs);
  entitlement.saleMode = std::move(saleMode);
  entitlement.source = *source;
  entitlement.expiresAt = expiresAt;
  entitlement.subscriptionStatus = *subscriptionStatus;
  entitlement.licenseKeys = std::move(*licenseKeys);
  entitlement.acquiredAt = *acquiredAt;
  return entitlement;
}

std::optional<SnapshotTrial> parseTrial(
  const JsonValue& value,
  std::string& message
) {
  if (value.type != JsonValue::Type::Object) {
    message = "License snapshot trial must be an object.";
    return std::nullopt;
  }

  SnapshotTrial trial;
  auto productId = requiredString(value, "productId", message);
  auto packId = optionalNullableString(value, "packId", true, message);
  if (!message.empty()) {
    return std::nullopt;
  }
  auto packIds = requiredStringArray(value, "packIds", message);
  auto packs = optionalPackArray(value, "packs", message);
  auto startedAt = requiredIsoTime(value, "startedAt", message);
  auto expiresAt = requiredIsoTime(value, "expiresAt", message);
  auto status = requiredString(value, "status", message);

  if (!productId || !packIds || !packs || !startedAt || !expiresAt || !status) {
    return std::nullopt;
  }

  if (*status != "active" && *status != "expired") {
    message = "License snapshot trial status is unknown.";
    return std::nullopt;
  }

  trial.productId = *productId;
  trial.packId = std::move(packId);
  trial.packIds = std::move(*packIds);
  trial.packs = std::move(*packs);
  trial.startedAt = *startedAt;
  trial.expiresAt = *expiresAt;
  trial.status = *status;
  return trial;
}

std::string lowerHex(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::optional<LicenseSnapshot> parseSnapshotPayload(
  const std::string& payloadJson,
  std::string& message
) {
  JsonValue payload;
  try {
    payload = JsonParser(payloadJson).parse();
  } catch (const std::exception& exception) {
    message = exception.what();
    return std::nullopt;
  }

  if (payload.type != JsonValue::Type::Object) {
    message = "License snapshot payload must be an object.";
    return std::nullopt;
  }

  LicenseSnapshot snapshot;
  auto schemaVersion = requiredInt(payload, "schemaVersion", message);
  if (!schemaVersion) {
    return std::nullopt;
  }
  if (*schemaVersion != kSupportedSchemaVersion) {
    message = "License snapshot schemaVersion is not supported.";
    return std::nullopt;
  }

  auto issuer = requiredString(payload, "issuer", message);
  auto sub = requiredString(payload, "sub", message);
  auto iat = requiredEpochSeconds(payload, "iat", message);
  auto exp = requiredEpochSeconds(payload, "exp", message);
  auto offlineGracePeriodDays = requiredInt(payload, "offlineGracePeriodDays", message);
  auto offlineGraceExpiresAt = requiredEpochSeconds(payload, "offlineGraceExpiresAt", message);
  const JsonValue* entitlementsValue = requiredField(payload, "entitlements", message);
  const JsonValue* trialsValue = requiredField(payload, "trials", message);

  if (!issuer || !sub || !iat || !exp || !offlineGracePeriodDays ||
      !offlineGraceExpiresAt || entitlementsValue == nullptr || trialsValue == nullptr) {
    return std::nullopt;
  }

  if (entitlementsValue->type != JsonValue::Type::Array) {
    message = "License snapshot entitlements must be an array.";
    return std::nullopt;
  }
  if (trialsValue->type != JsonValue::Type::Array) {
    message = "License snapshot trials must be an array.";
    return std::nullopt;
  }

  snapshot.schemaVersion = *schemaVersion;
  snapshot.issuer = *issuer;
  snapshot.sub = *sub;
  snapshot.iat = *iat;
  snapshot.exp = *exp;
  snapshot.offlineGracePeriodDays = *offlineGracePeriodDays;
  snapshot.offlineGraceExpiresAt = *offlineGraceExpiresAt;

  snapshot.entitlements.reserve(entitlementsValue->arrayValue.size());
  for (const auto& entitlementValue : entitlementsValue->arrayValue) {
    auto entitlement = parseEntitlement(entitlementValue, message);
    if (!entitlement) {
      return std::nullopt;
    }
    snapshot.entitlements.push_back(std::move(*entitlement));
  }

  snapshot.trials.reserve(trialsValue->arrayValue.size());
  for (const auto& trialValue : trialsValue->arrayValue) {
    auto trial = parseTrial(trialValue, message);
    if (!trial) {
      return std::nullopt;
    }
    snapshot.trials.push_back(std::move(*trial));
  }

  return snapshot;
}

SnapshotVerifyResult verifyLicenseSnapshotWithKey(
  const std::string& jws,
  const std::string& publicKeyPem
) {
  SnapshotVerifyResult result;
  const auto decoded = decodeSnapshotJws(jws, result);
  if (!decoded) {
    return result;
  }

  if (!verifyDecodedJws(*decoded, publicKeyPem, result)) {
    return result;
  }

  std::string message;
  auto snapshot = parseSnapshotPayload(decoded->payloadJson, message);
  if (!snapshot) {
    return failure(ErrorCode::InvalidSignature, message);
  }

  result.ok = true;
  result.error = ErrorCode::None;
  result.snapshot = std::move(snapshot);
  return result;
}

} // namespace

SnapshotVerifyResult verifyLicenseSnapshot(
  const std::string& jws,
  const std::string& publicKeyPem
) {
  return verifyLicenseSnapshotWithKey(jws, publicKeyPem);
}

SnapshotVerifyResult verifyLicenseSnapshot(
  const std::string& jws,
  const std::map<std::string, std::string>& keysetByKid
) {
  SnapshotVerifyResult result;
  const auto decoded = decodeSnapshotJws(jws, result);
  if (!decoded) {
    return result;
  }

  const std::string* selectedPublicKeyPem = nullptr;
  const auto kid = snapshotKid(decoded->header);
  if (kid) {
    const auto found = keysetByKid.find(*kid);
    if (found == keysetByKid.end()) {
      return failure(ErrorCode::InvalidSignature, kMissingKidErrorMessage);
    }
    selectedPublicKeyPem = &found->second;
  } else {
    if (keysetByKid.size() != 1) {
      return failure(
        ErrorCode::InvalidSignature,
        "License snapshot header kid is required when keyset size is not one."
      );
    }
    selectedPublicKeyPem = &keysetByKid.begin()->second;
  }

  if (!verifyDecodedJws(*decoded, *selectedPublicKeyPem, result)) {
    return result;
  }

  std::string message;
  auto snapshot = parseSnapshotPayload(decoded->payloadJson, message);
  if (!snapshot) {
    return failure(ErrorCode::InvalidSignature, message);
  }

  result.ok = true;
  result.error = ErrorCode::None;
  result.snapshot = std::move(snapshot);
  return result;
}

PackAccess evaluatePack(
  const LicenseSnapshot& snap,
  const std::string& packId,
  std::chrono::system_clock::time_point now
) {
  PackAccess access;
  access.snapshotFresh = now <= snap.exp;
  access.withinOfflineGrace = now <= snap.offlineGraceExpiresAt;

  if (packId.empty()) {
    return access;
  }

  for (const auto& entitlement : snap.entitlements) {
    if (std::find(entitlement.packIds.begin(), entitlement.packIds.end(), packId) ==
        entitlement.packIds.end()) {
      continue;
    }

    access.entitled = true;
    access.accessAllowedNow = !entitlement.expiresAt || now < *entitlement.expiresAt;
    access.source = entitlement.source;
    access.expiresAt = entitlement.expiresAt;
    access.matchedProductId = entitlement.productId;
    return access;
  }

  for (const auto& trial : snap.trials) {
    if (std::find(trial.packIds.begin(), trial.packIds.end(), packId) ==
        trial.packIds.end()) {
      continue;
    }

    access.entitled = true;
    access.accessAllowedNow = now < trial.expiresAt;
    access.source = SnapshotSource::Free;
    access.expiresAt = trial.expiresAt;
    access.matchedProductId = trial.productId;
    return access;
  }

  return access;
}

PackContentResult verifyPackContent(
  const LicenseSnapshot& snapshot,
  const std::string& packId,
  const std::string& fileSha256Hex
) {
  const auto evaluatePackHash = [&](const SnapshotPack& pack) -> std::optional<PackContentResult> {
    if (pack.packId != packId) {
      return std::nullopt;
    }
    if (!pack.contentHash) {
      return PackContentResult::Unknown;
    }

    return lowerHex(*pack.contentHash) == lowerHex(fileSha256Hex)
      ? PackContentResult::Match
      : PackContentResult::Mismatch;
  };

  for (const auto& entitlement : snapshot.entitlements) {
    for (const auto& pack : entitlement.packs) {
      if (const auto result = evaluatePackHash(pack)) {
        return *result;
      }
    }
  }

  for (const auto& trial : snapshot.trials) {
    for (const auto& pack : trial.packs) {
      if (const auto result = evaluatePackHash(pack)) {
        return *result;
      }
    }
  }

  return PackContentResult::Unknown;
}

} // namespace otomarket::license
