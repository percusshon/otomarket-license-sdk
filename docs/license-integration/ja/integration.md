<!-- lang-switch -->
[English](../en/integration.md) · **日本語**

# SDK 組み込みガイド

> 用語集: **[やさしい用語集](./glossary.md)**。

> **AI で組み込む**: このガイドと [SDK README](../../../README.md) を Cursor / Claude / ChatGPT などの AI アシスタントに渡せば、組み込みコードを生成・適用してもらえます。専門知識は不要です。
>
> **C++/JUCE 以外のプラグイン**は、[REST API ガイド（どの言語でも）](./rest-api.md)を参照してください（同じライセンス API をどの言語からでも直接呼べます）。

SDK（Software Development Kit）は、あなたのプラグインやアプリに組み込む C++/JUCE 向けの小さなライブラリです。これを通じてプラグインが OtoMarket のライセンス API と通信し、購入者のライセンスを**認証（activate）・確認（verify）・解除（deactivate）**します。自前でライセンスサーバや通信処理を実装する必要はなく、3つの関数を呼ぶだけで認証が付きます（オフラインでも署名済みライセンスで確認可能）。

SDK のダウンロードは1つで共通です。**コアだけで組み込む（認証画面は自作）か、既製の認証パネルを有効化して使うか**を、ビルド時に選びます。

**成功の合図**: 正しい（または sandbox）キーで認証した後、`otoIsLicensed()` が `true` を返します。認証パネルを使う場合は、パネルが「Licensed」と台数/期限を表示します。

VST3/AU プラグインへ組み込む場合は、オーディオスレッドでの呼び出しや複数インスタンスのキャッシュ競合に注意が必要です。先に [プラグイン統合ガイド](./plugin.md) も確認してください。

## できること

- C++/JUCE 向けのドロップイン SDK。オンライン認証とオフラインの署名ライセンス検証に対応し、利用台数・有効期限・失効を管理できます。
- Layer 1 API エンドポイント:
  - `POST /api/license/v1/activate`
  - `POST /api/license/v1/verify`
  - `POST /api/license/v1/deactivate`
- （任意）既製の JUCE 認証パネル `LicenseActivationPanel`。キー入力・状態表示・解除ができ、UI を自作する必要がありません。多言語の panel strings を `LicenseActivationStrings` で差し替え可能（de / es / pt-BR / zh-CN / fr / ru / ko の初期値は machine-translated で native review 待ち）。

## 手順（コア導入）

1. 商品の **License 連携**ページから製品の `productKey`（= `Product.id`）をコピーします。
2. 初めて組み込む場合は、sandbox の `productKey` と `licenseKey` で[クイックスタート](./quickstart.md)を実行します。
3. [SDK README](../../../README.md#cmake-取り込み) に従って SDK を追加します。
   - CMake を使わない Projucer プロジェクトは、下の「Projucer（CMake を使わない場合）」を参照してください。
4. SDK を次の値で設定します:
   - `baseUrl`（例 `https://otomarket.jp/api/license/v1`）
   - `publicKeyPem`（OtoMarket の Ed25519 署名公開鍵。本番リリースは下の「本番ビルドへの公開鍵の埋め込み」を参照）
   - `cachePath`
   - HTTP トランスポート
5. SDK の3つの製品向け関数を呼びます:
   - `otoActivate(productKey, licenseKey, machineName)`
   - `otoIsLicensed()`
   - `otoDeactivate()`
6. `otoIsLicensed()` で製品機能をゲートします。

ここまでで認証は動きます。キー入力欄や状態表示などの**認証画面はご自身で実装**します。既製の画面を使いたい場合は、次の「認証パネル」を有効化してください。

## 任意: 既製の認証パネル（UI を自作しない）

同じ SDK で、ビルド時に認証パネルのターゲットを有効化すると、キー入力・状態表示・解除ができる既製の JUCE パネルを使えます。

1. JUCE アダプタと認証パネルのターゲットを有効化します。
2. 認証パネルのターゲットを link します。
3. `productKey` と machine name を指定して panel を作成します。
4. 設定画面・ライセンス画面・初回起動フローなどに panel を mount します。

パネルはコアと同じ activation / deactivation を呼びます。どの機能をライセンス状態でゲートするかは、引き続きアプリ側が決めます。

## CMake 参考

コアのみ（直接 vendoring）:

```cmake
add_subdirectory(path/to/otomarket-license-sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk)
```

JUCE HTTP トランスポート:

```cmake
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
add_subdirectory(path/to/otomarket-license-sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk_juce)
```

既製の認証パネルも使う場合:

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

## Projucer（CMake を使わない場合）

CMake を使える場合は、前節の CMake 取り込みを推奨します。Projucer のみで管理している既存プロジェクトでは、SDK を vendoring して以下を手動で追加してください。

1. Projucer の対象 Exporter（Xcode / Visual Studio など）で Header Search Paths に次を追加します。
   - `packages/license-sdk-cpp/include`
   - `packages/license-sdk-cpp/vendor/tweetnacl`
2. Projucer の **Add Existing Files** で、コア実装をプロジェクトに追加します。
   - `packages/license-sdk-cpp/src/LicenseSdk.cpp`
   - `packages/license-sdk-cpp/src/LicenseActivationUi.cpp`
   - `packages/license-sdk-cpp/src/tweetnacl_randombytes.cpp`
   - `packages/license-sdk-cpp/vendor/tweetnacl/tweetnacl.c`
3. C++17 が有効になっていることを確認します。`std::filesystem` を使うため、古い toolchain では追加の linker 設定が必要な場合があります。
4. オンライン認証には `otomarket::license::HttpTransport` が必要です。コア SDK は JUCE 非依存なので、JUCE を使わない場合は `postJson()` と、per-creator keyset を使うなら `getJson()` を自前実装してください。
5. JUCE を使う場合は `#include <otomarket/license/JuceHttpTransport.h>` して `JuceHttpTransport` を使えます。現在の `JuceHttpTransport` は header-only なので追加の `.cpp` はありませんが、JUCE module `juce_core` が必要です。
6. 既製の `LicenseActivationPanel` を使う場合は、さらに `packages/license-sdk-cpp/src/LicenseActivationPanel.cpp` を **Add Existing Files** で追加し、JUCE module `juce_gui_basics` を有効にします。スタンドアロンウィンドウや SDK 付属サンプルと同じ形で `juce::JUCEApplication` / `juce::DocumentWindow` を使う場合は `juce_gui_extra` も有効にしてください。

## パネル参考

```cpp
#include <otomarket/license/LicenseActivationPanel.h>

otomarket::license::LicenseActivationPanelOptions options;
options.productId = "your-product-key";
options.machineName = "Studio Mac";
options.strings = otomarket::license::englishLicenseActivationStrings();

auto* panel = new otomarket::license::LicenseActivationPanel(licenses, options);
addAndMakeVisible(panel);
```

FetchContent・find_package・テスト・packaging・全例は SDK README を参照してください。

## 本番ビルドへの公開鍵の埋め込み

開発中の疎通確認では `LICENSE_SIGNING_PUBLIC_KEY` などの環境変数から `Config.publicKeyPem` に渡して構いません。ただし、出荷した VST3/AU プラグインのエンドユーザー環境にはその環境変数が無いため、本番リリースビルドでは公開鍵をプラグインのバイナリに埋め込んでください。

公開鍵は商品の **License 連携**ページからコピーできます。画面のコピー値は PEM ヘッダ/フッタを除いた base64 本文です。SDK 側は `publicKeyPem` を normalize し、PEM 形式、PEM ヘッダなしの本文、raw 32 bytes の base64、64 桁 hex を受け付けるため、コピーした値をそのまま `Config.publicKeyPem` に渡せます。アプリ側で `-----BEGIN PUBLIC KEY-----` / `-----END PUBLIC KEY-----` を付け直す必要はありません。

C++ ソース定数として埋め込む最小例:

```cpp
namespace {

constexpr const char* kProductKey = "your-product-key";
constexpr const char* kPublicKeyPem = R"PEM(MCowBQYDK2VwAyEAxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx=)PEM";

} // namespace

otomarket::license::Config config;
config.baseUrl = "https://otomarket.jp/api/license/v1";
config.publicKeyPem = kPublicKeyPem;
config.expectedProductId = kProductKey;
config.cachePath = otomarket::license::defaultCachePath("YourPluginName");
config.http = std::make_shared<otomarket::license::JuceHttpTransport>();
```

JUCE の BinaryData リソースとして埋め込む最小例:

```cpp
// Projucer の Binary Resources に OtoMarketPublicKey.pem.txt を追加する。
// ファイル内容は License 連携ページからコピーした公開鍵本文、または full PEM。
const auto publicKey = juce::String::fromUTF8(
  BinaryData::OtoMarketPublicKey_pem_txt,
  BinaryData::OtoMarketPublicKey_pem_txtSize
).trim().toStdString();

otomarket::license::Config config;
config.baseUrl = "https://otomarket.jp/api/license/v1";
config.publicKeyPem = publicKey;
config.expectedProductId = "your-product-key";
config.cachePath = otomarket::license::defaultCachePath("YourPluginName");
config.http = std::make_shared<otomarket::license::JuceHttpTransport>();
```

公開鍵は秘密ではないため、プラグインへ埋め込んで安全です。一方で、`LICENSE_SIGNING_PRIVATE_KEY` などの秘密鍵・秘密情報は絶対に埋め込み、表示、配布しないでください。OtoMarket がクリエイター別の署名鍵（`kid` 付きトークン）を使う場合、SDK は `keysUrl`（既定 `${baseUrl}/keys`）から公開鍵セットを自動取得し、`keysetTtl` に従って更新します。

## 公開鍵の扱い

`Config.publicKeyPem` には公開鍵のみを渡します。`LICENSE_SIGNING_PRIVATE_KEY` は絶対に埋め込み・表示しません。

OtoMarket がクリエイター別の署名鍵（`kid` 付きトークン）を使う場合でも、**SDK が公開鍵セットを `…/keys` から自動取得**するので、設定は `publicKeyPem` を渡すだけで済みます（詳細は[用語集](glossary.md)の「公開鍵セット」を参照）。
