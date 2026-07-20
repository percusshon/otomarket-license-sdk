<!-- lang-switch -->
**English** · [日本語](../ja/plugin.md)

# VST3/AU Plugin Integration Guide

> For the overall SDK setup flow, read the [SDK integration guide](./integration.md) first. Existing JUCE projects that do not use CMake should also read "Projucer (without CMake)" in that guide.

This page covers the extra care needed when integrating the OtoMarket C++ license SDK into a JUCE plugin (VST3/AU). Compared with a standalone app, audio-thread behavior and multiple plugin instances matter more.

## Do Not Call From The Audio Thread

Do not call `otoIsLicensed()`, `otoActivate()`, or `otoDeactivate()` from `processBlock()`. `otoIsLicensed()` reads the local cache and verifies the signature, `productId`, `machineId`, and expiry. If the verification deadline has passed, it attempts online `verify` and writes the refreshed cache on success. `otoActivate()` and `otoDeactivate()` also perform file IO and network requests.

In a plugin, run license checks when the editor opens, during initialization, or from background work started by the message thread. Pass only the result into `processBlock()` through `std::atomic<bool>` or a similar real-time-safe value. `processBlock()` should only read that bool.

## Multiple Instances And cachePath

When the same plugin is inserted multiple times in the same DAW project, each instance usually reads and writes the same `cachePath`. The cache is product-level license state, so do not split it into one path per plugin instance. Instead, centralize the writers.

Recommended designs:

- Share one `otomarket::license::Client` through a process-local singleton or similar service, and serialize activation / verify / deactivation in one place.
- Let each instance read only the result stored in `std::atomic<bool>`, and centralize activation / deactivation UI in one place such as a settings tab or the currently open editor.

`otoIsLicensed()` can also perform online `verify` and cache writes when re-verification is due, so avoid running it concurrently from multiple plugin instances. If you share a `Client`, also avoid calling into that same `Client` concurrently from your own worker thread and `LicenseActivationPanel`'s background job.

## Placing LicenseActivationPanel

If you use the ready-made UI, place `LicenseActivationPanel` in a fixed region of `PluginEditor`, a settings tab, or an About/License tab. The panel runs `otoIsLicensed()`, `otoActivate()`, and `otoDeactivate()` on an internal background thread and returns only the result to the UI thread, so it does not block the UI.

The panel is a component, not the whole plugin UI. It owns license-key entry, Activate / Deactivate, and license status display. The plugin should still own the background, product logo, tabs, and fixed-size editor layout.

## Minimal Skeleton

This is the minimal shape to adapt into an existing `PluginProcessor` / `PluginEditor`. Required `AudioProcessor` overrides and the real audio processing body are omitted.

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
  options.strings = license::englishLicenseActivationStrings();
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

    // Put licensed audio processing here.
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

In this example, the default `LicenseActivationPanelOptions::checkOnConstruct = true` makes the panel start an asynchronous status check when the editor opens. If you need the license state before the editor is opened, call `otoIsLicensed()` from a plugin-owned worker thread / thread pool with a well-defined lifetime, then update `licensedForAudio_` when it completes. Still never call SDK APIs directly from `processBlock()`.

## Integration Notes

JUCE HTTP transport requires `juce_core`. `LicenseActivationPanel` requires `juce_gui_basics`. With CMake, link `otomarket::license_sdk_juce` and `otomarket::license_sdk_activation_panel`. With Projucer, follow the "Projucer (without CMake)" manual file and JUCE module setup in the [SDK integration guide](./integration.md).
