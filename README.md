# OtoMarket License SDK for C++

Amplarium / DrumLoom などの JUCE / VST3 / AU 製品に、OtoMarket の
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

セットアップ手順とサンドボックスキー / Setup steps and sandbox keys:
**[クイックスタート（日本語）](docs/license-integration/ja/quickstart.md)** ·
**[Quickstart (English)](docs/license-integration/en/quickstart.md)**

## CMake 取り込み

### add_subdirectory

公開リポジトリを clone するか Releases の zip を展開して配置した SDK を直接使う場合:

```cmake
add_subdirectory(path/to/otomarket-license-sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk)
```

JUCE HTTP アダプタも使う場合:

```cmake
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
add_subdirectory(path/to/otomarket-license-sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk_juce)
```

埋め込みアクティベーションパネルも使う場合:

```cmake
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
set(OTOMARKET_LICENSE_SDK_BUILD_ACTIVATION_PANEL ON CACHE BOOL "")
add_subdirectory(path/to/otomarket-license-sdk)
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
  GIT_REPOSITORY https://github.com/percusshon/otomarket-license-sdk.git
  GIT_TAG v1.1.0
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
- `publicKeyPem`: OtoMarket の Ed25519 signing public key（`kid` 無しの従来トークン検証＋フォールバック用）
- `cachePath`: ライセンスキャッシュ保存先（keyset キャッシュも同じ場所の兄弟ファイルに保存）
- `http`: オンライン activation / verify / deactivate / keyset 取得用 transport

任意（省略可・既定で動作）：

- `maxOffline`: オフライン信頼の上限（`issuedAt` 起点）。実効再検証期限は
  `min(verifyAfter, issuedAt + maxOffline)`。既定 30 日。鍵漏洩時の被害を限定します。
- `keysUrl`: 公開鍵セット（JWKS）エンドポイント。空なら `${baseUrl}/keys`。
- `keysetTtl`: 取得済み keyset を再取得するまでの間隔（オンライン時）。既定 24 時間。
  `maxOffline` とは独立。
- `keyset`: `kid → 公開鍵` を事前に渡す場合に使用（通常は自動取得で不要）。

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
config.cachePath = otomarket::license::defaultCachePath("Amplarium");
config.http = std::make_shared<otomarket::license::JuceHttpTransport>();

otomarket::license::Client licenses(config);
```

> **`Client` を唯一の真実源（source of truth）に**：ライセンス状態と署名トークンの
> キャッシュは `Client` が own します。別途プレーンな entitlements JSON などの
> 並行キャッシュリーダーを作らないでください（状態が二重化し、最初の実アダプター
> OtoSpace で最大の混乱要因になりました）。

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
期限を検証します。実効再検証期限 `min(verifyAfter, issuedAt + maxOffline)`（既定上限 30 日）
を過ぎている場合はオンライン `verify` を試み、API が一時的に落ちている場合は
`Config.verifyRetryGrace` の範囲内だけ `true` を返します（既定 72 時間）。
`issuedAt` が未来のトークンは拒否します。

### per-creator 鍵（keyset / JWKS）

OtoMarket はクリエイターごとの鍵でライセンスを署名でき、鍵漏洩の影響をその
クリエイターの商品だけに局所化できます。その場合トークン header に `kid` が入ります。
**自動**で処理されます（設定は `publicKeyPem` を渡すだけで済みます）：

- Client は `keysUrl`（既定 `${baseUrl}/keys`）から keyset を取得し `Config.keyset`
  に統合・`cachePath` 兄弟ファイルにキャッシュ。TTL（`keysetTtl`、既定 24h）＋
  activate/verify のついで＋未知 `kid` 検出時に再取得します。
- `kid` 付きトークンは keyset の該当鍵で、`kid` 無し（従来）トークンは
  `publicKeyPem` で検証します。
- 取得は best-effort：エンドポイント不通や `getJson` 未対応の transport でも
  `publicKeyPem` にフォールバックし、`otoIsLicensed()` をオフラインで `false` に
  したりライセンスを失効させたりしません。自前 transport で keyset を使うには
  `HttpTransport::getJson` を override してください（`JuceHttpTransport` は実装済み）。

## HTTP リクエスト仕様（activate / verify / deactivate）

`Client` がこれらを内部で組み立てて送信するため通常は直接扱う必要はありませんが、
他言語からの結線や疎通確認のために契約を明記します（値は OtoMarket 側の
`activateLicenseInputSchema` / `verifyLicenseInputSchema` に基づく）。

`POST {baseUrl}/{activate|verify|deactivate}`、Content-Type は `application/json`：

| フィールド | activate | verify / deactivate | 内容 |
|---|---|---|---|
| `productId` | 必須* | 必須* | 商品 ID（最大 128 文字）。`productKey` はエイリアスで、どちらか一方があればよい |
| `productKey` | 必須* | 必須* | `productId` の別名（上と排他で「どちらか必須」） |
| `licenseKey` | 必須 | 必須 | 発行済みライセンスキー（最大 256 文字） |
| `machineId` | 必須 | 必須 | 安定した hashed マシン ID（`deriveMachineId` 推奨、最大 256 文字） |
| `machineName` | 任意 | — | 表示用ラベル（activate のみ） |

\* `productId` と `productKey` の**どちらか一方は必須**。両方欠けると
`400`（`productId or productKey is required.`）。

`activate` 成功レスポンス（要点）：

```json
{ "ok": true, "license": "<header.payload.signature>", "seatsUsed": 1, "maxActivations": 5, "expiresAt": null }
```

`license` は EdDSA 署名トークンで、公開鍵で `verifyLicenseToken(...)` 検証します。
空ボディ `{}` を送ると `400 INVALID_REQUEST_BODY`（＝API に到達している合図）。
失敗時の `error` コードは `SEAT_LIMIT` / `EXPIRED` / `REVOKED` / `NOT_FOUND` /
`PRODUCT_MISMATCH` など。

## Lv2 埋め込みパネル

JUCE アプリでは `LicenseActivationPanel` を既存の設定画面やウィンドウに
`addAndMakeVisible` で置くだけで、キー入力、Activate / Deactivate、状態表示、
エラー文言表示を組み込めます。通信はバックグラウンド thread で行い、UI thread
をブロックしません。

```cpp
#include <otomarket/license/LicenseActivationPanel.h>

otomarket::license::LicenseActivationPanelOptions options;
options.productId = "amplarium-product-id";
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

### 初回ゲート方式

初回起動時にメイン UI へライセンス部品を混ぜたくない場合は、
`LicenseActivationPanel` をルートコンポーネント全面に表示し、認証後にメイン UI
へ差し替えてください。パネルが表示するのは見出し、ライセンスキー入力、
Activate / Deactivate、状態、エラー文言です。背景、製品ロゴ、ブランド配色は
host 側で描けるため、メイン UI と同じ見た目に統一できます。

```cpp
class AppRoot final : public juce::Component {
public:
  AppRoot(otomarket::license::Client& licenses,
          otomarket::license::LicenseActivationPanelOptions options)
    : licenses_(licenses),
      options_(std::move(options)) {
    if (licenses_.otoIsLicensed()) {
      showMainUi();
    } else {
      showGate();
    }
  }

  void resized() override {
    if (panel) {
      panel->setBounds(getLocalBounds());
    }
    if (mainUi) {
      mainUi->setBounds(getLocalBounds());
    }
  }

private:
  void showGate() {
    mainUi.reset();
    if (!panel) {
      panel = std::make_unique<otomarket::license::LicenseActivationPanel>(
        licenses_,
        options_
      );
      panel->onLicenseStateChanged = [this](otomarket::license::LicenseState state) {
        if (state == otomarket::license::LicenseState::Licensed
            || state == otomarket::license::LicenseState::LicensedOfflineGrace) {
          showMainUi();
        } else {
          showGate();
        }
      };
      addAndMakeVisible(*panel);
    }
    resized();
  }

  void showMainUi() {
    panel.reset();
    if (!mainUi) {
      mainUi = std::make_unique<juce::Label>("main", "Main UI");
      addAndMakeVisible(*mainUi);
    }
    resized();
  }

  otomarket::license::Client& licenses_;
  otomarket::license::LicenseActivationPanelOptions options_;
  std::unique_ptr<otomarket::license::LicenseActivationPanel> panel;
  std::unique_ptr<juce::Component> mainUi;
};
```

2 回目以降は `otoIsLicensed()` が即 `true` になればメイン UI へ直行できます。
ライセンス UI は初回 activation と、キャッシュ失効・deactivate・再認証が必要な
状態のときだけ表示してください。完全なサンプルは
`examples/FirstRunGateExample.cpp` です。

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

## Amplarium / DrumLoom 組み込み雛形

```cpp
class LicenseService {
public:
  explicit LicenseService(otomarket::license::Config config)
    : client_(std::move(config)) {}

  bool isUnlocked() {
    return client_.otoIsLicensed();
  }

  otomarket::license::ActivateResult activate(const std::string& key) {
    return client_.otoActivate("amplarium-product-id", key, "User machine");
  }

  otomarket::license::DeactivateResult deactivate() {
    return client_.otoDeactivate();
  }

private:
  otomarket::license::Client client_;
};
```

Amplarium / DrumLoom では、既存 UI に独自のキー入力欄を置く場合は上記 3 関数を
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
