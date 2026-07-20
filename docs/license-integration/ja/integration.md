<!-- lang-switch -->
[English](../en/integration.md) · **日本語**

# SDK 組み込みガイド

> 用語集: **[用語集](./glossary.md)**。

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

## 組み込みチェックリスト（ゲート実装まで含めて完了です）

SDK は「ライセンスが今有効か」という判定を返すだけで、**製品を止める処理はあなたのゲート実装が担います**。ゲートを書かなければ、認証に失敗しても製品はそのまま動いてしまいます。リリース前に、次の3点を必ず確認してください。

1. **組み込み**: sandbox キーで `otoActivate` が成功し、`otoIsLicensed()` が `true` になる。
2. **ゲート**: `otoIsLicensed()` が `false` のとき、製品の主要機能が使えない。方式は製品に合わせて選びます（起動時の全面ゲート／音声処理のバイパス／保存の無効化など）。認証パネルを使う場合は、状態変化に合わせてゲートを追随させてください。
3. **「止まる側」の動作確認**: 有効なキーで使えることに加えて、①未認証の状態 ②解除（deactivate）した後 ③サブスク商品なら失効後、にゲートが実際に効くことを確認します。**「使える側」だけを確認してリリースしないでください。**

## 本番内テスト（公開前に、配布するバイナリそのもので確認する）

テスト用の別環境や環境別ビルドは**不要**です。本番の OtoMarket の中で、公開前の商品をそのままテストできます。

1. **商品を下書きのままアップロード**: 本番向けの値（`productKey`・本番の公開鍵・本番 `baseUrl`）でビルドしたバイナリを、商品を公開せずにファイル登録します。
2. **テストキーを発行**: 商品編集画面の License 連携セクションで「テストキーを発行」を押します。購入なしで最大5本、認証台数は商品設定どおり、**90日で自動失効**します。不要になったらいつでも失効できます。
3. **自分でダウンロード**: 商品バージョン管理画面のファイル一覧から、本人はいつでもダウンロードできます（ウイルススキャン合格後）。商品が非公開でも可能です。
4. **実バイナリで認証テスト**: ダウンロードしたバイナリにテストキーを入力し、認証 → ゲート解放 → 解除 → ゲート施錠、を上のチェックリストどおり確認します。認証APIは商品の公開状態を見ないため、下書きのままで本番とまったく同じ動きをします。
5. **そのまま公開**: テストに使ったバイナリを差し替えることなく公開できます。テストキーは購入者の画面・売上・精算に一切影響しません。

> かつては staging（テスト環境）用に別ビルドを作る方法を案内していましたが、環境ごとに商品IDが変わるため「テストしたものと配るものが別物になる」問題がありました。現在はこの本番内テストが推奨手順です。

## 環境ごとの差し替え早見表

本番と検証環境（staging）は、データベース・署名鍵・商品IDがすべて分離しています。そのため **SDK にビルド時に埋め込む次の3つの値が環境ごとに異なり**、片方の環境で発行したライセンスはもう片方のビルドでは認証できません（署名鍵が違うため、混在させても偽物として拒否されるだけで事故にはなりません）。

| ビルド時に埋め込む値 | 本番 | 検証環境（staging） |
| --- | --- | --- |
| `Config.baseUrl` | `https://otomarket.jp/api/license/v1` | 運営がご案内する staging の URL |
| `productKey`（= `Product.id`） | 本番商品のID | staging商品のID（**別採番**） |
| `Config.publicKeyPem`（署名公開鍵） | 本番の公開鍵 | staging の公開鍵（**別物**） |

- `productKey` と公開鍵は、それぞれの環境の商品編集画面の **License 連携**ページからコピーできます。
- **どちらの環境で検証するかで、ビルドを作り分ける必要があります。** ただし前ページの「本番内テスト」を使えば、**実際に配布する本番向けビルド1本だけ**で公開前の認証テストが完結するため、検証環境用の別ビルドは通常不要です。検証環境（staging）側で SDK の認証まで確認したい場合にのみ、上表の staging 値で別ビルドを作成してください。

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

OtoMarket がクリエイター別の署名鍵（`kid` 付きトークン）を使う場合、SDK は公開鍵セットを `…/keys` から自動取得します。`kid` 無しの旧トークンだけが `publicKeyPem` を使い、`kid` 付きトークンには keyset 内の一致する鍵が必須です（未知 `kid` は `publicKeyPem` にフォールバックしません）。取得済み keyset キャッシュがあればオフライン検証できますが、初回取得前や鍵ローテーション直後はオンライン接続が必要です（詳細は[用語集](glossary.md)の「公開鍵セット」を参照）。

## 後から外す・無効化する（着脱できます）

SDK は「署名付きライセンスを検証する」ための部品で、**パックや音源データを暗号化してロックするものではありません**。そのため、一度組み込んだ後でも外せますし、外しても**パック自体は問題なく読み込めます**（ライセンス確認をしなくなるだけです）。OtoMarket のライセンスは「破られない暗号」ではなく、摩擦・失効・台数制限による**軽い抑止**として設計されているため、利用側がロックインされることはありません。

- **外すと変わること**: そのビルドではトライアル期限・認証台数・失効などの確認が無くなり、無制限に動作します。
- **すでに配布済みのビルド**: SDK を含んだまま動き続けます。外す効果は次に出すビルドからで、過去に配ったコピーを後から無効化することはできません。
- **サーバ側のロックはありません**: `…/api/license/v1`（`/me` や activate）を呼ばなくなるだけです。

### おすすめ: ビルドフラグで着脱できるようにする

完全にコードを削除する代わりに、コンパイル時のフラグで検証処理を囲っておくと、含める／外すをビルド設定だけで切り替えられます（毎回コードを削らずに済みます）。

```cpp
#ifdef OTOMARKET_LICENSE_ENABLED
  otomarket::license::Config config;
  config.baseUrl = "https://otomarket.jp/api/license/v1";
  config.publicKeyPem = kPublicKeyPem;
  config.expectedProductId = kProductKey;
  config.cachePath = otomarket::license::defaultCachePath("YourPluginName");
  config.http = std::make_shared<otomarket::license::JuceHttpTransport>();
  // ... activation / verification ...
#endif
```

```cmake
option(OTOMARKET_LICENSE_ENABLED "Enable OtoMarket license checks" ON)
if (OTOMARKET_LICENSE_ENABLED)
  target_compile_definitions(YourPlugin PRIVATE OTOMARKET_LICENSE_ENABLED=1)
  # ここで OtoMarket license SDK をリンクする
endif()
```

`-DOTOMARKET_LICENSE_ENABLED=OFF` でビルドすれば、SDK 無しのビルドが作れます。

### もっと軽くしたい場合

完全に外す以外に、**より軽い連携レベルに下げる**選択もできます（例: 署名付きライセンスファイルだけを使う [Level 0](lv0.md)）。レベルの違いは[概要](overview.md)を参照してください。
