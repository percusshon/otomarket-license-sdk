<!-- lang-switch -->
**English** · [日本語](../ja/integration.md)

# SDK Integration Guide

> Glossary: **[glossary](./glossary.md)**.

> **Integrating with AI**: paste this guide and the [SDK README](../../../README.md) into an AI assistant (Cursor / Claude / ChatGPT) and have it generate and apply the integration code. No expertise required.
>
> **Not a C++/JUCE plugin?** See the [REST API guide (any language)](./rest-api.md) — the same license API can be called directly from any language.

The SDK (Software Development Kit) is a small C++/JUCE library you embed in your plugin or app. Through it, your plugin talks to OtoMarket's license API to **activate, verify, and deactivate** a buyer's license. You don't implement a license server or the networking yourself — you just call three functions to turn licensing on (offline verification with a signed license is supported too).

The SDK is a single shared download. You choose **whether to integrate the core only (build your own UI) or enable the ready-made activation panel** at build time.

**You'll know it worked when**: `otoIsLicensed()` returns `true` after activating with a valid (or sandbox) key. If you use the panel, it shows "Licensed" with the seat/expiry.

If you are integrating a VST3/AU plugin, also read the [plugin integration guide](./plugin.md) first. Plugin builds need extra care around audio-thread calls and shared license-cache writes across multiple plugin instances.

## What OtoMarket Provides

- A drop-in C++/JUCE SDK. It supports online activation and offline signed-license verification, and lets you manage seats, expiry, and revocation.
- Layer 1 API endpoints:
  - `POST /api/license/v1/activate`
  - `POST /api/license/v1/verify`
  - `POST /api/license/v1/deactivate`
- (Optional) the ready-made JUCE `LicenseActivationPanel` for key entry, status display, and deactivation, so you don't build the UI. Panel strings are replaceable through `LicenseActivationStrings` (the de / es / pt-BR / zh-CN / fr / ru / ko defaults are machine-translated and pending native review).

## Steps (core integration)

1. Copy the product's `productKey` (= `Product.id`) from the product's **License integration** page.
2. If you are integrating for the first time, run the
   [quickstart](./quickstart.md) with the sandbox `productKey` and `licenseKey`.
3. Add the SDK using the
   [SDK README](../../../README.md#cmake-取り込み).
   - For Projucer projects that do not use CMake, see "Projucer (without CMake)" below.
4. Configure the SDK with:
   - `baseUrl`, for example `https://otomarket.jp/api/license/v1`
   - `publicKeyPem`, using OtoMarket's Ed25519 signing public key. For release builds, see "Embedding The Public Key In Production Builds" below.
   - `cachePath`
   - an HTTP transport
5. Call the SDK's three product-facing functions:
   - `otoActivate(productKey, licenseKey, machineName)`
   - `otoIsLicensed()`
   - `otoDeactivate()`
6. Gate product functionality on `otoIsLicensed()`.

At this point activation works. You implement the activation screen (key entry, status) yourself. If you want a ready-made screen instead, enable the activation panel below.

## Integration checklist (you are not done until gating is in)

The SDK only answers "is this license currently valid" — **stopping the product is your gating code's job**. If you skip the gate, the product keeps working even when activation fails. Verify all three points before you ship:

1. **Integration**: `otoActivate` succeeds with the sandbox key and `otoIsLicensed()` returns `true`.
2. **Gating**: with `otoIsLicensed()` returning `false`, the product's core features are unusable. Pick the mechanism that fits your product (a full launch gate, bypassing audio processing, disabling save, …). If you use the activation panel, follow its state changes with your gate.
3. **Verify the "stops working" side**: besides confirming a valid key works, confirm the gate actually engages ① before activation, ② after `deactivate`, and ③ for subscription products, after the license lapses. **Do not ship after testing only the happy path.**

## In-production testing (verify the exact binary you will ship, before publishing)

You do **not** need a separate test environment or per-environment builds. You can test an unpublished product inside production OtoMarket itself.

1. **Upload while still a draft**: build with your production values (`productKey`, the production public key, the production `baseUrl`) and register the files without publishing the product.
2. **Issue test keys**: in the product editor's license integration section, press "Issue test key". Up to five keys, no purchase required, device limits follow your product settings, and every key **auto-expires after 90 days**. Revoke any key at any time.
3. **Download it yourself**: from the product's version management page, you can always download your own files (once the virus scan passes) — even while the product is unpublished.
4. **Activation-test the real binary**: enter a test key into the downloaded binary and walk the checklist above — activate → gate opens → deactivate → gate closes. The activation API never checks publication state, so a draft product behaves exactly like a published one.
5. **Publish as-is**: ship the very binary you just tested, unchanged. Test keys never appear on buyer-facing screens and never touch sales or settlements.

> We previously suggested building a separate staging binary for testing, but product IDs differ per environment, so the binary you tested was never the binary you shipped. In-production testing is now the recommended path.

## Per-environment values (quick reference)

Production and the staging (test) environment have separate databases, signing keys, and product IDs. So **the three values you embed into the SDK at build time differ per environment**, and a license issued in one environment cannot activate a build for the other (the signing keys differ, so a mismatched build simply rejects it as invalid — no harm done).

| Value embedded at build time | Production | Staging (test) |
| --- | --- | --- |
| `Config.baseUrl` | `https://otomarket.jp/api/license/v1` | the staging URL we provide |
| `productKey` (= `Product.id`) | production product's ID | staging product's ID (**separately assigned**) |
| `Config.publicKeyPem` (signing public key) | production public key | staging public key (**different**) |

- Copy `productKey` and the public key from each environment's product editor, under **License integration**.
- **You need a separate build per environment you want to test in.** But with "in-production testing" (previous section) the single production build you actually ship covers pre-publication activation testing, so a staging-specific build is usually unnecessary — build one with the staging values above only if you specifically want to verify activation inside the staging environment.

## Optional: ready-made activation panel (don't build the UI)

With the same SDK, enable the activation-panel target at build time to use a ready-made JUCE panel for key entry, status display, and deactivation.

1. Enable the JUCE adapter and the activation panel targets.
2. Link the activation panel target.
3. Create the panel with the `productKey` and a machine name.
4. Mount the panel in a settings window, license dialog, or first-run activation flow.

The panel calls the same activation/deactivation paths as the core. The consuming app still decides which product features are gated by license state.

## CMake Reference

Core only (direct vendoring):

```cmake
add_subdirectory(path/to/otomarket-license-sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk)
```

JUCE HTTP transport:

```cmake
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
add_subdirectory(path/to/otomarket-license-sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk_juce)
```

With the ready-made activation panel:

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

## Projucer (without CMake)

Use the CMake integration above when your project can use CMake. For an existing Projucer-only project, vendor the SDK and add the files manually.

1. In each Projucer Exporter you ship (Xcode, Visual Studio, etc.), add these Header Search Paths:
   - `packages/license-sdk-cpp/include`
   - `packages/license-sdk-cpp/vendor/tweetnacl`
2. Use **Add Existing Files** to add the core implementation files:
   - `packages/license-sdk-cpp/src/LicenseSdk.cpp`
   - `packages/license-sdk-cpp/src/LicenseActivationUi.cpp`
   - `packages/license-sdk-cpp/src/tweetnacl_randombytes.cpp`
   - `packages/license-sdk-cpp/vendor/tweetnacl/tweetnacl.c`
3. Make sure C++17 is enabled. The SDK uses `std::filesystem`, so older toolchains may need extra linker settings.
4. Online activation needs an `otomarket::license::HttpTransport`. The core SDK does not depend on JUCE; if you do not use JUCE, implement `postJson()` yourself and also `getJson()` if you want per-creator keysets.
5. In JUCE projects, include `#include <otomarket/license/JuceHttpTransport.h>` and use `JuceHttpTransport`. The current `JuceHttpTransport` is header-only, so there is no extra `.cpp` to add, but the JUCE module `juce_core` is required.
6. If you use the ready-made `LicenseActivationPanel`, also add `packages/license-sdk-cpp/src/LicenseActivationPanel.cpp` with **Add Existing Files** and enable the JUCE module `juce_gui_basics`. Enable `juce_gui_extra` too when you use `juce::JUCEApplication` / `juce::DocumentWindow`, as the SDK standalone examples do.

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

See the SDK README for FetchContent, find_package, tests, packaging, and full examples.

## Embedding The Public Key In Production Builds

During development and connectivity checks, it is fine to pass `Config.publicKeyPem` from an environment variable such as `LICENSE_SIGNING_PUBLIC_KEY`. A shipped VST3/AU plugin cannot assume that environment variable exists on an end user's machine, so production release builds should embed the public key in the plugin binary.

Copy the public key from the product's **License integration** page. The copied value is the base64 body without PEM header/footer lines. The SDK normalizes `publicKeyPem` and accepts full PEM, the PEM body without headers, raw 32-byte base64, and 64-character hex, so you can pass the copied value directly to `Config.publicKeyPem`. You do not need to add `-----BEGIN PUBLIC KEY-----` / `-----END PUBLIC KEY-----` in your app.

Minimal C++ source constant:

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

Minimal JUCE BinaryData resource:

```cpp
// Add OtoMarketPublicKey.pem.txt to Projucer's Binary Resources.
// The file can contain the copied public-key body or a full PEM.
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

The public key is not secret and is safe to embed in the plugin. Never embed, display, or distribute private secrets such as `LICENSE_SIGNING_PRIVATE_KEY`. When OtoMarket uses per-creator signing keys (`kid` tokens), the SDK fetches the public keyset from `keysUrl` (default `${baseUrl}/keys`) and refreshes it according to `keysetTtl`.

## Public Key Handling

Pass only the public key to `Config.publicKeyPem`. Never embed or display `LICENSE_SIGNING_PRIVATE_KEY`.

When OtoMarket uses per-creator signing keys (`kid` tokens), the SDK fetches the public keyset from `…/keys` automatically. Only legacy tokens without `kid` use `publicKeyPem`; a token with `kid` requires a matching keyset entry and never falls back to `publicKeyPem`. A fetched keyset cache supports offline verification, while the first fetch and the period immediately after key rotation require an online connection. See "Public keyset" in the [glossary](glossary.md).

## Removing Or Disabling The SDK Later

The SDK only **verifies signed licenses** — it does **not** encrypt or lock your packs / audio content. So you can remove it after integrating, and **your packs still load fine** without it (you just stop checking licenses). OtoMarket licensing is designed as a **lightweight deterrent** (friction, revocation, seat limits), not unbreakable DRM, so you are never locked in.

- **What changes when you remove it**: that build no longer checks trial expiry, seat limits, or revocation, so it runs unrestricted.
- **Builds you already shipped**: they keep checking (the SDK is still compiled in). Removal only affects the next build you ship — you cannot retroactively disable copies users already have.
- **No server-side lock**: you simply stop calling `…/api/license/v1` (`/me`, activate).

### Recommended: make it toggleable with a build flag

Instead of deleting the code, wrap the verification in a compile-time flag so you can include/exclude it from build settings alone (no need to rip code out each time).

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
  # link the OtoMarket license SDK here
endif()
```

Building with `-DOTOMARKET_LICENSE_ENABLED=OFF` produces a build without the SDK.

### If you only want something lighter

Instead of removing it entirely, you can **drop to a lighter integration level** (for example [Level 0](lv0.md), which uses only a signed license file). See the [overview](overview.md) for how the levels differ.
