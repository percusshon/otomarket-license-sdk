<!-- lang-switch -->
[English](../en/quickstart.md) · **日本語**

# OtoMarket License SDK クイックスタート

> 用語集: **[やさしい用語集](./glossary.md)**。

**成功の合図**: `verify` が `"ok": true` を返します。空ボディで `INVALID_REQUEST_BODY` が返る場合も、API には到達できている合図です。

このクイックスタートでは、実購入なしで OtoMarket のライセンス activation をテストする方法を説明します。Layer 1 license API と
[`packages/license-sdk-cpp`](../../..) の C++ SDK を使います。

## Sandbox 値

次の値は development seed で作成されます。ローカル環境またはセルフホスト環境での連携テストに安全に使えます。

| 名前 | 値 |
| --- | --- |
| `productKey` | `otomarket-sandbox-license-sdk` |
| `licenseKey` | `OTOMARKET-SANDBOX-LICENSE-SDK` |
| Local `baseUrl` | `http://localhost:3010/api/license/v1` |

sandbox ライセンスは通常の `License` 行ですが、`OrderItem` や `Entitlement` はありません。上記の非公開 sandbox 商品だけに紐づいているため、実商品の購入証明として使ってはいけません。

hosted staging API では、この repository seed だけではこれらの行は自動作成されません。staging `baseUrl` でこのキーを activation するには、OtoMarket が同じ sandbox 商品とライセンスを staging database に provision しておく必要があります。

テスト時は、安定した一意の `machineId` を使ってください。共有 staging key では、`test-machine` のような汎用値は避け、`sandbox-yourname-macbook` のような値を使います。

## Server Setup

ローカル API を確認する場合:

1. `DATABASE_URL` を設定します。
2. `LICENSE_API_ENABLED=true` を設定します。
3. サーバー側に `LICENSE_SIGNING_PRIVATE_KEY` を設定します。
4. クライアントアプリが `LICENSE_SIGNING_PUBLIC_KEY` を使える状態にします。
5. seed を実行し、sandbox 商品とライセンスを作成します。

```bash
npm run prisma:generate
npm exec prisma -- db seed
npm run dev
```

`LICENSE_API_ENABLED` が `true` でない間、API は 404 を返します。

## cURL で Activate する

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

成功レスポンスには次が含まれます。

- `ok: true`
- `license`: signed license token
- `seatsUsed`
- `maxActivations`
- `expiresAt`

## Verify と Deactivate

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

## C++ SDK を追加する

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

商品が JUCE HTTP transport を使う場合:

```cmake
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
FetchContent_MakeAvailable(otomarket_license_sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk_juce)
```

## SDK の追加方法を選ぶ

SDK をビルドに取り込む方法は3つあります。同期方法とアクセス要件に合わせて選んでください。

| 方法 | 向いている場面 | トレードオフ |
| --- | --- | --- |
| `add_subdirectory`（vendored copy） | 自己完結した repository にしたい、build/CI 時に OtoMarket monorepo へアクセスできない | SDK source を `third_party/` 配下にコピーし、手動で更新する |
| git submodule | upstream を追跡し、同期を保ちたい | clone/CI 時に repository access が必要 |
| `FetchContent`（public SDK repo） | tagged release に最も簡単に pin したい | configure 時に network fetch が発生する |

`FetchContent` / submodule 用の public SDK repo は
`https://github.com/percusshon/otomarket-license-sdk` です。上の例でもこの repo を使っています。

### Opt-In Flags の内側に置く

通常ビルドや開発ビルドに影響しないよう、ライセンス付きビルドが必要なときだけ SDK を追加します。

```cmake
option(MYPLUGIN_LICENSE_LIVE "License activation against the real API" OFF)

if(MYPLUGIN_LICENSE_LIVE)
  set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
  set(OTOMARKET_LICENSE_SDK_BUILD_ACTIVATION_PANEL ON CACHE BOOL "")
  add_subdirectory(third_party/otomarket-license-sdk)  # or FetchContent
endif()
```

### Link Targets

| Target | 提供するもの |
| --- | --- |
| `otomarket::license_sdk` | core `Client`（JUCE なし） |
| `otomarket::license_sdk_juce` | `JuceHttpTransport`（実 HTTP。LIVE のみ） |
| `otomarket::license_sdk_activation_panel` | `Client` + 既製の `LicenseActivationPanel` |

`JuceHttpTransport` を使う場合は、tweetnacl include dir
`third_party/otomarket-license-sdk/vendor/tweetnacl` も target に追加してください。

> 参考: OtoSpace（最初の実アダプター）の全結線手順は OtoSound
> `docs/otomarket-license-sdk-integration.md` に記録されています。

## Client を設定する

次の snippet は `JuceHttpTransport` を使うため、上で説明したとおり
`otomarket::license_sdk_juce` を有効化して link してください。

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

## 鍵の扱い

配布する商品に入れてよいのは Ed25519 公開鍵だけです。次の情報は絶対に埋め込み・表示しないでください。

- `LICENSE_SIGNING_PRIVATE_KEY`
- Stripe secrets
- storage keys
- R2/S3 credentials

公開鍵は activation と verify のレスポンスで返る signed license token を検証するためのものです。有効な OtoMarket license を発行することはできません。

## 本番連携チェックリスト

sandbox から実商品へ切り替える前に確認してください。

1. `productKey` を実商品の `Product.id` に置き換えます。
2. 購入者が OtoMarket library で確認できる実際の `licenseKey` を使います。
3. 公開鍵の扱いは同じ経路のままにします。
4. OtoMarket から提供された production または staging の `baseUrl` を使います。
5. 同じ user と device では `machineId` を安定させます。
6. ライセンスが必要な商品機能だけを gate します。
