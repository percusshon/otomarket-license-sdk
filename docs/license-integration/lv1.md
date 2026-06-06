# Lv1: Drop-In SDK

> Glossary / 用語集: **[plain-words terms](./glossary.md)** (EN/JA).

**In plain words / やさしく言うと**: EN — add our C++/JUCE SDK and call **three
functions** to turn licensing on; works offline too. JA — C++/JUCE の SDK を入れ、**3つの関数**を呼ぶだけで認証が付く（オフラインでも動く）。
**You'll know it worked when / 成功の合図**: EN — `otoIsLicensed()` returns
`true` after activating with a valid (or sandbox) key. JA — 正しい（またはsandbox）キーで認証後 `otoIsLicensed()` が `true` を返す。

Lv1 adds lightweight enforcement with the C++ SDK. The product calls the SDK for
online activation, offline signed-license verification, and deactivation.

## What OtoMarket Provides

- `productKey` for the product. This is `Product.id`.
- C++ SDK in [`packages/license-sdk-cpp`](../..).
- The Ed25519 signing public key configuration path.
- Layer 1 API endpoints:
  - `POST /api/license/v1/activate`
  - `POST /api/license/v1/verify`
  - `POST /api/license/v1/deactivate`

## Creator / Developer Steps

1. Copy the product's `productKey` from the product edit page.
2. If you are integrating for the first time, run the
   [English quickstart](./quickstart.md) with the sandbox `productKey` and
   `licenseKey`.
3. Add the SDK using the
   [SDK README](../../README.md#cmake-取り込み).
4. Configure the SDK with:
   - `baseUrl`, for example `https://otomarket.jp/api/license/v1`
   - `publicKeyPem`, using OtoMarket's Ed25519 signing public key
   - `cachePath`
   - an HTTP transport
5. Call the SDK's three product-facing functions:
   - `otoActivate(productKey, licenseKey, machineName)`
   - `otoIsLicensed()`
   - `otoDeactivate()`
6. Gate product functionality on `otoIsLicensed()`.

## CMake Reference

For direct vendoring:

```cmake
add_subdirectory(path/to/otomarket-license-sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk)
```

For JUCE HTTP transport:

```cmake
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
add_subdirectory(path/to/otomarket-license-sdk)
target_link_libraries(YourPluginTarget PRIVATE otomarket::license_sdk_juce)
```

See the SDK README for FetchContent, find_package, tests, packaging, and full
examples.

## Public Key Handling

Pass only the public key to `Config.publicKeyPem`. Never embed or display
`LICENSE_SIGNING_PRIVATE_KEY`.

## Japanese Summary

Lv1 は C++ SDK を使った軽量な強制認証です。商品編集画面から `productKey`
（`Product.id`）をコピーし、SDK README に従って CMake で取り込みます。公開鍵だけを
`publicKeyPem` に渡し、`otoActivate` / `otoIsLicensed` / `otoDeactivate` を呼びます。
署名秘密鍵は絶対に配布しません。
