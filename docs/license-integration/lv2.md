# Lv2: SDK Plus Ready-Made UI

> Glossary / 用語集: **[plain-words terms](./glossary.md)** (EN/JA).

**In plain words / やさしく言うと**: EN — Lv1 plus a **ready-made activation
screen** (key box + status), so you build no UI. JA — Lv1 に**既製のアクティベーション画面**（キー入力＋状態表示）が付き、UIを自作しなくてよい。
**You'll know it worked when / 成功の合図**: EN — the panel shows "Licensed"
and the seat/expiry after entering a valid key. JA — 正しいキー入力後、パネルが「Licensed」と台数/期限を表示する。

Lv2 uses the same SDK enforcement as Lv1 and adds the ready-made JUCE activation
panel. Choose Lv2 when the product needs a complete key-entry and license-status
screen without building custom UI.

## What OtoMarket Provides

- Everything in [Lv1](./lv1.md).
- `LicenseActivationPanel` for JUCE applications and plug-ins.
- English, Japanese, German, Spanish, Brazilian Portuguese, Simplified Chinese,
  French, Russian, and Korean panel strings that can be replaced through
  `LicenseActivationStrings`. The de / es / pt-BR / zh-CN / fr / ru / ko
  defaults are machine-translated and pending native review.

## Creator / Developer Steps

1. Copy the product's `productKey` from the product edit page.
2. If you are integrating for the first time, run the
   [English quickstart](./quickstart.md) with the sandbox `productKey` and
   `licenseKey`.
3. Enable the JUCE adapter and activation panel targets before adding the SDK.
4. Link the activation panel target.
5. Create the panel with the product key and machine name.
6. Mount the panel in a settings window, license dialog, or first-run activation
   flow.

## CMake Reference

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

## Panel Reference

```cpp
#include <otomarket/license/LicenseActivationPanel.h>

otomarket::license::LicenseActivationPanelOptions options;
options.productId = "your-product-key";
options.machineName = "Studio Mac";
options.strings = otomarket::license::englishLicenseActivationStrings();

auto* panel = new otomarket::license::LicenseActivationPanel(licenses, options);
addAndMakeVisible(panel);
```

The panel calls the same activation/deactivation paths as Lv1. The consuming app
still decides which product features are gated by license state.

## Japanese Summary

Lv2 は Lv1 の SDK に既製の JUCE アクティベーションパネルを加える方式です。
キー入力、状態表示、解除 UI を自作せずに導入できます。`productKey` と公開鍵を設定し、
SDK README とこのガイドに従って panel target を有効化してください。署名秘密鍵は配布しません。
