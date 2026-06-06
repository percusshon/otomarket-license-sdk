# OtoMarket License SDK Quickstart

> Glossary / 用語集: **[plain-words terms](./glossary.md)** (EN/JA).

**In plain words / やさしく言うと**: EN — try activation end-to-end with a free
**sandbox** key, no real purchase, using `curl` then the SDK. JA — 無料の **sandbox** キーで、実購入なしに認証を最初から最後まで試す（まず `curl`、次に SDK）。
**You'll know it worked when / 成功の合図**: EN — `verify` returns
`"ok": true` (and an empty body returns `INVALID_REQUEST_BODY`, which means the
API is reachable). JA — `verify` が `"ok": true` を返す（空ボディなら `INVALID_REQUEST_BODY`＝APIに届いている合図）。

This quickstart shows how to test OtoMarket license activation without a real
purchase. It uses the Layer 1 license API and the C++ SDK in
[`packages/license-sdk-cpp`](../..).

## Sandbox Values

These values are created by the development seed and are safe to use for local
or self-hosted integration tests:

| Name | Value |
| --- | --- |
| `productKey` | `otomarket-sandbox-license-sdk` |
| `licenseKey` | `OTOMARKET-SANDBOX-LICENSE-SDK` |
| Local `baseUrl` | `http://localhost:3010/api/license/v1` |

The sandbox license is a normal `License` row with no `OrderItem` or
`Entitlement`. It is tied only to the hidden sandbox product above, so it must
not be used as proof of purchase for real products.

For the hosted staging API, these rows are not created automatically by this
repository seed. OtoMarket must provision the same sandbox product and license
in the staging database before the staging `baseUrl` can activate this key.

Use a stable, unique `machineId` during tests. For shared staging keys, avoid
generic values such as `test-machine`; use something like
`sandbox-yourname-macbook`.

## Server Setup

For a local API check:

1. Configure `DATABASE_URL`.
2. Configure `LICENSE_API_ENABLED=true`.
3. Configure `LICENSE_SIGNING_PRIVATE_KEY` on the server.
4. Keep `LICENSE_SIGNING_PUBLIC_KEY` available to the client app.
5. Run the seed so the sandbox product and license exist.

```bash
npm run prisma:generate
npm exec prisma -- db seed
npm run dev
```

The API returns 404 while `LICENSE_API_ENABLED` is not `true`.

## Activate With cURL

```bash
BASE_URL="http://localhost:3010/api/license/v1"

curl -sS -X POST "$BASE_URL/activate" \
  -H "Content-Type: application/json" \
  -d '{
    "productKey": "otomarket-sandbox-license-sdk",
    "licenseKey": "OTOMARKET-SANDBOX-LICENSE-SDK",
    "machineId": "sandbox-yourname-macbook",
    "machineName": "Studio Mac"
  }'
```

A successful response includes:

- `ok: true`
- `license`: signed license token
- `seatsUsed`
- `maxActivations`
- `expiresAt`

## Verify And Deactivate

```bash
curl -sS -X POST "$BASE_URL/verify" \
  -H "Content-Type: application/json" \
  -d '{
    "productKey": "otomarket-sandbox-license-sdk",
    "licenseKey": "OTOMARKET-SANDBOX-LICENSE-SDK",
    "machineId": "sandbox-yourname-macbook"
  }'

curl -sS -X POST "$BASE_URL/deactivate" \
  -H "Content-Type: application/json" \
  -d '{
    "productKey": "otomarket-sandbox-license-sdk",
    "licenseKey": "OTOMARKET-SANDBOX-LICENSE-SDK",
    "machineId": "sandbox-yourname-macbook"
  }'
```

## Add The C++ SDK

```cmake
include(FetchContent)

FetchContent_Declare(
  otomarket_license_sdk
  GIT_REPOSITORY https://github.com/percusshon/otomarket-license-sdk.git
  GIT_TAG v1.0.0
)

FetchContent_MakeAvailable(otomarket_license_sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk)
```

If your product uses JUCE HTTP transport:

```cmake
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
FetchContent_MakeAvailable(otomarket_license_sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk_juce)
```

## Configure The Client

The following snippet uses `JuceHttpTransport`, so enable and link
`otomarket::license_sdk_juce` as shown above.

```cpp
#include <cstdlib>
#include <memory>

#include <otomarket/license/LicenseSdk.h>
#include <otomarket/license/JuceHttpTransport.h>

constexpr const char* productKey = "otomarket-sandbox-license-sdk";
constexpr const char* licenseKey = "OTOMARKET-SANDBOX-LICENSE-SDK";

otomarket::license::Config config;
config.baseUrl = "http://localhost:3010/api/license/v1";
config.expectedProductId = productKey;
config.cachePath = otomarket::license::defaultCachePath("OtoMarketSandbox");
config.http = std::make_shared<otomarket::license::JuceHttpTransport>();

if (const char* publicKey = std::getenv("LICENSE_SIGNING_PUBLIC_KEY")) {
  config.publicKeyPem = publicKey;
}

otomarket::license::Client licenses(config);

auto activated = licenses.otoActivate(productKey, licenseKey, "Studio Mac");
if (!activated.ok) {
  auto reason = otomarket::license::toString(activated.error);
  // Show reason to the user or write it to a development log.
}

if (licenses.otoIsLicensed()) {
  // Enable licensed product features.
}
```

## Key Handling

Only the Ed25519 public key belongs in shipped products. Never embed or expose:

- `LICENSE_SIGNING_PRIVATE_KEY`
- Stripe secrets
- storage keys
- R2/S3 credentials

The public key verifies the signed license token returned by activation and
verify responses. It cannot mint valid OtoMarket licenses.

## Production Integration Checklist

Before switching from sandbox to a real product:

1. Replace `productKey` with the real product's `Product.id`.
2. Use the buyer's real `licenseKey` from their OtoMarket library.
3. Keep the same public key handling path.
4. Use the production or staging `baseUrl` supplied by OtoMarket.
5. Keep `machineId` stable for the same user and device.
6. Gate only the product features that require a license.
