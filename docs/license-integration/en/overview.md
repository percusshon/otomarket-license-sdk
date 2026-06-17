<!-- lang-switch -->
**English** · [日本語](../ja/overview.md)

# OtoMarket License Integration Overview

This guide explains the self-serve license integration options creators can
choose for each product. The source design is
[`docs/license-verification-api-design.md`](../../license-verification-api-design.md)
§2 and §3.

> New here? Read the **[plain-words glossary](./glossary.md)** first. Every term
> is explained with analogies.

> **Integrating with AI**: you don't need to be an expert — paste the relevant guide ([SDK integration guide](./integration.md) or [REST API guide](./rest-api.md)) and the [SDK README](../../../README.md) into an AI assistant (Cursor / Claude / ChatGPT) and have it generate the integration code. Verify connectivity first with the sandbox key in the [quickstart](./quickstart.md).

## In Plain Words

OtoMarket can be the **license backend for your own plugin or app**. You do
**not** build a license server or pay the iLok "tax." Anyone can put OtoMarket
licensing on the plugin they made: create a product, get a `productKey`, then
either drop in one of our SDKs (**C++/JUCE** or **JS/TS**) or call our **REST
API** from any language.

**Coverage (SDK vs. API)**

- **SDKs are available for C++/JUCE and JavaScript/TypeScript.** The C++/JUCE
  drop-in SDK targets plugins (VST3/AU/Standalone); the JS/TS SDK targets
  JavaScript/TypeScript apps (Node, Electron, web). A C# SDK is being added.
- The **"license API" (REST/JSON) works in any language** that can make HTTP
  requests. Non-C++/JUCE products need no SDK — call the API directly (see the
  [REST API guide](./rest-api.md)).
- Online activation (check with the API each time) needs no cryptography.
  **Offline verification** (validate a signed license locally when the API is
  down) only requires Ed25519 signature verification in your language — a
  standard, widely available capability.

**OtoRig packs**

In the OtoRig ecosystem the **host (OtoRig Player) integrates the SDK once and
checks every pack's license**, so pack creators do not implement licensing
themselves.

## What Creators Choose

Each product can use one of these client-side integration levels. The backend
license key, activation API, and signed license token are the same foundation
for all levels; the difference is how much client code the creator's product
uses.

| Level | Use when | What OtoMarket provides | Enforcement |
| --- | --- | --- | --- |
| Lv0 | You only need purchase proof. | A buyer-visible license key in the OtoMarket library. | None |
| SDK integration | You want license activation in an app or plug-in. | C++ SDK, `productKey`, signing public key instructions (optionally the ready-made JUCE activation panel). | Seats, expiry, revocation |

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
[`packages/license-sdk-cpp`](../../..). Its
[`README.md`](../../../README.md) covers CMake, the
JUCE adapter, the embedded activation panel, examples, tests, and packaging.

The JavaScript/TypeScript SDK lives in
[`packages/license-sdk-js`](../../../packages/license-sdk-js) (Node, Electron,
web). See its [`README.md`](../../../packages/license-sdk-js/README.md) for usage.

Start with the [SDK quickstart](./quickstart.md) if you want to activate a
sandbox license before wiring a real product.

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

Guides:

- [SDK quickstart](./quickstart.md)
- [Lv0 purchase proof](./lv0.md)
- [SDK integration guide (C++/JUCE)](./integration.md)
- [VST3/AU plugin integration guide](./plugin.md)
- [REST API guide (any language)](./rest-api.md)
