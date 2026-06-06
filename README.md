# OtoMarket License SDK for C++

OtoRig / DrumLoom などの JUCE / VST3 / AU 製品に、OtoMarket の
`/api/license/v1/activate|verify|deactivate` を数行で組み込むための
ドロップイン SDK です。

## できること

- 初回オンライン activation
- Ed25519 署名済みライセンストークンのオフライン検証
- `verifyAfter` 到来時のオンライン verify と短期 retry grace
- 署名ライセンスキャッシュの保存/読込/削除
- product ごとの hashed `machineId` 導出
- HTTP transport の差し替え
- JUCE `juce::URL` / `WebInputStream` ベースの任意アダプタ
- JUCE の任意埋め込みアクティベーションパネル
- `find_package(otomarket-license-sdk)` 用の install/export

SDK コアは JUCE に依存しません。JUCE を使わない CTest でビルドできます。

English setup steps and sandbox keys are available in
[`docs/license-integration/quickstart.md`](../../docs/license-integration/quickstart.md).

## CMake 取り込み

### add_subdirectory

リポジトリ内または vendoring した SDK を直接使う場合:

```cmake
add_subdirectory(path/to/otomarket/packages/license-sdk-cpp)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk)
```

JUCE HTTP アダプタも使う場合:

```cmake
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
add_subdirectory(path/to/otomarket/packages/license-sdk-cpp)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk_juce)
```

埋め込みアクティベーションパネルも使う場合:

```cmake
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
set(OTOMARKET_LICENSE_SDK_BUILD_ACTIVATION_PANEL ON CACHE BOOL "")
add_subdirectory(path/to/otomarket/packages/license-sdk-cpp)
target_link_libraries(YourPluginTarget
  PRIVATE
    otomarket::license_sdk_juce
    otomarket::license_sdk_activation_panel
)
```

### FetchContent

外部プロジェクトから GitHub 経由で取り込む場合:

```cmake
include(FetchContent)

FetchContent_Declare(
  otomarket_license_sdk
  GIT_REPOSITORY https://github.com/percusshon/otomarket.git
  GIT_TAG main
  SOURCE_SUBDIR packages/license-sdk-cpp
)

FetchContent_MakeAvailable(otomarket_license_sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk)
```

JUCE パネルも使う場合は `FetchContent_MakeAvailable` の前に
`OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER` と
`OTOMARKET_LICENSE_SDK_BUILD_ACTIVATION_PANEL` を `ON` にしてください。

### find_package

インストール済み SDK を使う場合:

```bash
cmake -S packages/license-sdk-cpp -B packages/license-sdk-cpp/build-install \
  -DOTOMARKET_LICENSE_SDK_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=/path/to/otomarket-license-sdk-install
cmake --build packages/license-sdk-cpp/build-install
cmake --install packages/license-sdk-cpp/build-install
```

利用側:

```cmake
find_package(otomarket-license-sdk CONFIG REQUIRED)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk)
```

## 設定

初期化時に以下を渡します。

- `baseUrl`: 例 `https://staging.example.test/api/license/v1`
- `publicKeyPem`: OtoMarket の Ed25519 signing public key
- `cachePath`: ライセンスキャッシュ保存先
- `http`: オンライン activation / verify / deactivate 用 transport

staging の公開鍵値は SDK にハードコードしません。OtoMarket 側で発行した
公開鍵を環境変数 `LICENSE_SIGNING_PUBLIC_KEY` に設定し、アプリ側の設定注入で
`Config.publicKeyPem` に渡してください。

```cpp
#include <cstdlib>
#include <memory>

#include <otomarket/license/LicenseSdk.h>
#include <otomarket/license/JuceHttpTransport.h>

otomarket::license::Config config;
config.baseUrl = "https://staging.example.test/api/license/v1";
if (const char* publicKey = std::getenv("LICENSE_SIGNING_PUBLIC_KEY")) {
  config.publicKeyPem = publicKey;
}
config.cachePath = otomarket::license::defaultCachePath("OtoRig");
config.http = std::make_shared<otomarket::license::JuceHttpTransport>();

otomarket::license::Client licenses(config);
```

## 3 関数の使い方

```cpp
auto result = licenses.otoActivate("product-id", userEnteredLicenseKey, "Studio Mac");

if (!result.ok) {
  auto reason = otomarket::license::toString(result.error);
  // SEAT_LIMIT / EXPIRED / REVOKED / NOT_FOUND / PRODUCT_MISMATCH など
}

if (licenses.otoIsLicensed()) {
  // プラグイン機能を有効化
}

auto deactivated = licenses.otoDeactivate();
```

`otoIsLicensed()` はローカルキャッシュを読み、署名・productId・machineId・
期限を検証します。`verifyAfter` を過ぎている場合はオンライン `verify` を試み、
API が一時的に落ちている場合は `Config.verifyRetryGrace` の範囲内だけ
`true` を返します。既定値は 72 時間です。

## Lv2 埋め込みパネル

JUCE アプリでは `LicenseActivationPanel` を既存の設定画面やウィンドウに
`addAndMakeVisible` で置くだけで、キー入力、Activate / Deactivate、状態表示、
エラー文言表示を組み込めます。通信はバックグラウンド thread で行い、UI thread
をブロックしません。

```cpp
#include <otomarket/license/LicenseActivationPanel.h>

otomarket::license::LicenseActivationPanelOptions options;
options.productId = "otorig-product-id";
options.machineName = "Studio Mac";
options.strings = otomarket::license::japaneseLicenseActivationStrings();

auto* panel = new otomarket::license::LicenseActivationPanel(licenses, options);
addAndMakeVisible(panel);
```

表示文言は `LicenseActivationStrings` を渡すだけで差し替えできます。既定では
`englishLicenseActivationStrings()`、`japaneseLicenseActivationStrings()`、
`germanLicenseActivationStrings()`、`spanishLicenseActivationStrings()`、
`brazilianPortugueseLicenseActivationStrings()`、
`simplifiedChineseLicenseActivationStrings()`、`frenchLicenseActivationStrings()`、
`russianLicenseActivationStrings()`、`koreanLicenseActivationStrings()` を用意しています。
de / es / pt-BR / zh-CN / fr / ru / ko は機械翻訳・ネイティブ校正待ちです。

## machineId

既定では productId を salt にして、ホスト名、ユーザー、ホームディレクトリ、
Linux の `/etc/machine-id` などから作った fingerprint を SHA-512 でハッシュし、
`oto2_...` の形式で送信します。生のハードウェア ID は送信しません。

消費側でより安定した fingerprint を持っている場合は、以下のどちらかを注入できます。

```cpp
config.machineFingerprint = "your-stable-local-fingerprint";
// または
config.machineId = "prehashed-machine-id";
```

## OtoRig / DrumLoom 組み込み雛形

```cpp
class LicenseService {
public:
  explicit LicenseService(otomarket::license::Config config)
    : client_(std::move(config)) {}

  bool isUnlocked() {
    return client_.otoIsLicensed();
  }

  otomarket::license::ActivateResult activate(const std::string& key) {
    return client_.otoActivate("otorig-product-id", key, "User machine");
  }

  otomarket::license::DeactivateResult deactivate() {
    return client_.otoDeactivate();
  }

private:
  otomarket::license::Client client_;
};
```

OtoRig / DrumLoom では、既存 UI に独自のキー入力欄を置く場合は上記 3 関数を
呼びます。既製 UI でよい場合は `examples/StandaloneLicensePanelExample.cpp` と
同じ形で `LicenseActivationPanel` を設定画面に埋め込んでください。

example をビルドする場合は JUCE が必要です。

```bash
cmake -S packages/license-sdk-cpp -B packages/license-sdk-cpp/build-examples \
  -DOTOMARKET_LICENSE_SDK_BUILD_EXAMPLES=ON
cmake --build packages/license-sdk-cpp/build-examples
```

## テスト

```bash
cmake -S packages/license-sdk-cpp -B packages/license-sdk-cpp/build \
  -DOTOMARKET_LICENSE_SDK_BUILD_TESTS=ON
cmake --build packages/license-sdk-cpp/build
ctest --test-dir packages/license-sdk-cpp/build --output-on-failure
```

テストでは TweetNaCl で Ed25519 鍵ペアを生成し、JWT風
`header.payload.signature` トークンを署名して SDK で検証します。HTTP は
`MockHttpTransport` で `activate` / `verify` / `deactivate` フローを検証します。

## Vendor

Ed25519 実装は `vendor/tweetnacl/` に同梱しています。TweetNaCl は upstream が
public-domain C library として配布している単一ファイル実装で、libsodium 等には
依存しません。出典は `vendor/tweetnacl/NOTICE` を参照してください。
