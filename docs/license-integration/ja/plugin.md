<!-- lang-switch -->
[English](../en/plugin.md) · **日本語**

# VST3/AU プラグイン統合ガイド

> 先に全体の流れを確認したい場合は [SDK 組み込みガイド](./integration.md) を参照してください。CMake を使わない既存 JUCE プロジェクトは、同ガイドの「Projucer（CMake を使わない場合）」も確認してください。

このページは、自作 JUCE プラグイン（VST3/AU）に OtoMarket C++ ライセンス SDK を組み込むときの注意点です。Standalone アプリよりも、オーディオスレッドと複数インスタンスの扱いが重要になります。

## オーディオスレッドから呼ばない

`otoIsLicensed()`、`otoActivate()`、`otoDeactivate()` は `processBlock()` から呼ばないでください。`otoIsLicensed()` はローカルキャッシュを読み、署名、`productId`、`machineId`、期限を検証します。再検証期限を過ぎている場合はオンライン `verify` を試み、成功時はキャッシュも更新します。`otoActivate()` / `otoDeactivate()` もファイル IO とネットワーク通信を行います。

プラグインでは、ライセンス確認をエディタ表示時、初期化時、またはメッセージスレッドから起動したバックグラウンド処理で行い、結果だけを `std::atomic<bool>` などで `processBlock()` に渡してください。`processBlock()` 側はその bool を読むだけにします。

## 複数インスタンスと cachePath

同じ DAW プロジェクトに同じプラグインを複数挿すと、各インスタンスは通常同じ `cachePath` を読み書きします。`cachePath` は product 単位のライセンスキャッシュなので、インスタンスごとに別パスへ分けるのではなく、書き込み側を集約してください。

推奨は次のどちらかです。

- プロセス内シングルトンなどで `otomarket::license::Client` を 1 つ共有し、activation / verify / deactivation を 1 箇所で直列化する。
- 各インスタンスは `std::atomic<bool>` に保持した結果を読むだけにし、activation / deactivation UI は 1 箇所（設定タブや現在開いている editor）に集約する。

`otoIsLicensed()` も再検証時にはオンライン `verify` とキャッシュ更新を行うため、複数インスタンスから同時に走らせない設計にしてください。共有 `Client` を使う場合も、同じ `Client` に対してアプリ側の独自スレッドと `LicenseActivationPanel` のバックグラウンド処理を同時に走らせないようにします。

## LicenseActivationPanel の配置

既製 UI を使う場合は、`LicenseActivationPanel` を `PluginEditor` 内の固定領域、設定タブ、About/License タブなどに置きます。パネルは内部のバックグラウンド thread で `otoIsLicensed()` / `otoActivate()` / `otoDeactivate()` を呼び、結果だけを UI thread に戻すため、UI をブロックしません。

パネルは見た目全体を所有するものではなく、ライセンスキー入力、Activate / Deactivate、状態表示を持つ部品です。背景、製品ロゴ、タブ、固定サイズ editor のレイアウトはプラグイン側で管理してください。

## 最小スケルトン

以下は既存の `PluginProcessor` / `PluginEditor` に組み込むための最小形です。`AudioProcessor` の必須 override や音声処理本体は省略しています。

```cpp
#include <otomarket/license/JuceHttpTransport.h>
#include <otomarket/license/LicenseActivationPanel.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace license = otomarket::license;

namespace {

constexpr const char* kProductKey = "your-product-key";
constexpr const char* kPublicKeyPem = R"PEM(MCowBQYDK2VwAyEAxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx=)PEM";

license::Config makeLicenseConfig() {
  license::Config config;
  config.baseUrl = "https://otomarket.jp/api/license/v1";
  config.publicKeyPem = kPublicKeyPem;
  config.expectedProductId = kProductKey;
  config.cachePath = license::defaultCachePath("YourPluginName");
  config.http = std::make_shared<license::JuceHttpTransport>();
  return config;
}

license::LicenseActivationPanelOptions makePanelOptions() {
  license::LicenseActivationPanelOptions options;
  options.productId = kProductKey;
  options.machineName = juce::SystemStats::getComputerName().toStdString();
  options.strings = license::japaneseLicenseActivationStrings();
  return options;
}

bool allowsAudio(license::LicenseState state) {
  return state == license::LicenseState::Licensed
      || state == license::LicenseState::LicensedOfflineGrace;
}

} // namespace

class MyPluginProcessor final : public juce::AudioProcessor {
public:
  MyPluginProcessor()
    : licenses_(makeLicenseConfig()) {}

  license::Client& licenseClient() noexcept {
    return licenses_;
  }

  void setLicenseStateForAudio(license::LicenseState state) noexcept {
    licensedForAudio_.store(allowsAudio(state), std::memory_order_release);
  }

  bool isLicensedForAudio() const noexcept {
    return licensedForAudio_.load(std::memory_order_acquire);
  }

  void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
    if (!isLicensedForAudio()) {
      buffer.clear();
      return;
    }

    // ライセンス済みの音声処理だけをここに置く。
  }

private:
  std::atomic<bool> licensedForAudio_{false};
  license::Client licenses_;
};

class MyPluginEditor final : public juce::AudioProcessorEditor {
public:
  explicit MyPluginEditor(MyPluginProcessor& processor)
    : juce::AudioProcessorEditor(&processor),
      processor_(processor),
      licensePanel_(processor_.licenseClient(), makePanelOptions()) {
    addAndMakeVisible(licensePanel_);
    addAndMakeVisible(statusLabel_);

    licensePanel_.onLicenseStateChanged = [this](license::LicenseState state) {
      processor_.setLicenseStateForAudio(state);
      statusLabel_.setText(license::toString(state), juce::dontSendNotification);
    };

    setSize(640, 360);
  }

  void resized() override {
    auto bounds = getLocalBounds().reduced(16);
    statusLabel_.setBounds(bounds.removeFromTop(24));
    licensePanel_.setBounds(bounds.removeFromTop(220));
  }

private:
  MyPluginProcessor& processor_;
  license::LicenseActivationPanel licensePanel_;
  juce::Label statusLabel_;
};
```

この例では `LicenseActivationPanelOptions::checkOnConstruct` の既定値 `true` により、editor 表示時にパネルが非同期で状態確認を開始します。editor を開く前からライセンス状態を反映したい場合は、プラグイン側で寿命管理された worker thread / thread pool から `otoIsLicensed()` を呼び、完了後に `licensedForAudio_` を更新してください。その場合も `processBlock()` から SDK API を直接呼ばないでください。

## 取り込みメモ

JUCE HTTP transport を使う場合は `juce_core` が必要です。`LicenseActivationPanel` を使う場合は `juce_gui_basics` が必要です。CMake では `otomarket::license_sdk_juce` と `otomarket::license_sdk_activation_panel` を link します。Projucer の場合は [SDK 組み込みガイド](./integration.md) の「Projucer（CMake を使わない場合）」の手順で SDK の `.cpp` と必要な JUCE module を追加してください。
