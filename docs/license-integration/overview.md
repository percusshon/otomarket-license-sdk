# OtoMarket License Integration Overview

This guide explains the self-serve license integration options creators can
choose for each product. The source design is
[`docs/license-verification-api-design.md`](../license-verification-api-design.md)
§2 and §3.

> New here? Read the **[plain-words glossary](./glossary.md)** first — every term
> is explained with analogies, in English and Japanese.
> ／はじめての方は先に **[やさしい用語集](./glossary.md)**（たとえ話・日英）をどうぞ。

## In plain words / やさしい全体像

- EN: OtoMarket can be the **license backend for your own plugin or app**. You do
  **not** build a license server or pay the iLok "tax." Anyone can put OtoMarket
  licensing on the plugin they made: create a product → get a `productKey` →
  either drop in our **C++/JUCE SDK** or call our **REST API** from any language.
- JA: OtoMarket は **あなた自身のプラグイン/アプリのライセンス基盤**になれます。自前のライセンスサーバも iLok の“税金”も不要。**誰でも自作プラグインに OtoMarket ライセンスを載せられます**：商品を作る → `productKey` をもらう → **C++/JUCE は SDK をドロップイン**／**他言語は REST API を直接**呼ぶ。

**Coverage / 対応範囲**
- EN: **C++/JUCE plugins (VST3/AU/Standalone)** use the drop-in SDK today. Other
  languages call the language-neutral REST API now; **JS/TS, C# and more SDKs are
  being added**.
- JA: **C++/JUCE（VST3/AU/Standalone）は今すぐ SDK**。他言語は今は言語中立な REST API を直接。**JS/TS・C# 等の SDK も順次追加中**。

**OtoRig packs / OtoRig のパック**
- EN: In the OtoRig ecosystem the **host (OtoRig Player) integrates the SDK once
  and checks every pack's license**, so pack creators do not implement licensing
  themselves.
- JA: OtoRig では **ホスト（OtoRig Player）が SDK を1回組み込み、各パックのライセンスをまとめて検証**します。パック作者はライセンス実装をしなくてOK。

## What Creators Choose

Each product can use one of these client-side integration levels. The backend
license key, activation API, and signed license token are the same foundation
for all levels; the difference is how much client code the creator's product
uses.

| Level | Use when | What OtoMarket provides | Enforcement |
| --- | --- | --- | --- |
| Lv0 | You only need purchase proof. | A buyer-visible license key in the OtoMarket library. | None |
| Lv1 | You want lightweight activation in an app or plug-in. | C++ SDK, `productKey`, signing public key instructions. | Seats, expiry, revocation |
| Lv2 | You want a ready-made activation screen. | Lv1 plus the JUCE activation panel. | Seats, expiry, revocation |

## Product Key

`productKey` is the product's public identifier. In OtoMarket it is exactly
`Product.id`.

Creators can copy it from the product edit page under **License integration**.
It is safe to embed in a shipped app because it only identifies which product a
license belongs to. It is not an authorization secret.

## Keys And Secrets

Use only the Ed25519 signing public key in client products. The public key is
passed to the SDK as `Config.publicKeyPem`.

Never expose or ship:

- `LICENSE_SIGNING_PRIVATE_KEY`
- storage keys or object storage secrets
- Stripe secrets
- R2/S3 credentials

## SDK Distribution

The C++ SDK lives in
[`packages/license-sdk-cpp`](../..). Its
[`README.md`](../../README.md) covers CMake, the JUCE
adapter, the embedded activation panel, examples, tests, and packaging.

Start with the English
[SDK quickstart](./quickstart.md) if you want to activate a sandbox license
before wiring a real product.

## Sandbox Test Keys

The development seed creates a hidden sandbox product and shared test license:

| Name | Value |
| --- | --- |
| `productKey` | `otomarket-sandbox-license-sdk` |
| `licenseKey` | `OTOMARKET-SANDBOX-LICENSE-SDK` |

These keys are for local and self-hosted integration tests only. They are not
proof of purchase and should never gate a real product. Hosted staging needs the
same sandbox rows provisioned in the staging database before the staging API can
activate them.

Level guides:

- [English SDK quickstart](./quickstart.md)
- [Lv0 purchase proof](./lv0.md)
- [Lv1 drop-in SDK](./lv1.md)
- [Lv2 SDK plus ready-made UI](./lv2.md)

## Japanese Summary

商品ごとに Lv0/Lv1/Lv2 をクリエイターが選びます。`productKey` は
`Product.id` であり、公開してよい商品識別子です。SDK には公開鍵だけを渡します。
署名秘密鍵、storageKey、Stripe/R2 の秘密情報は表示・配布しません。
