<!-- lang-switch -->
**English** · [日本語](../ja/rest-api.md)

# License REST API (any language)

> Glossary: **[plain-words terms](./glossary.md)**.

Plugins or apps that aren't C++/JUCE can integrate licensing by calling **this REST API directly, in any language** (the C++ SDK calls the same API under the hood).

> **Integrating with AI**: paste this page and the [quickstart](./quickstart.md) into an AI assistant (Cursor / Claude / ChatGPT) and have it generate the integration code for your language. Verify connectivity first with the sandbox key in the [quickstart](./quickstart.md).

## Endpoints

The base URL is shown on the product's "License integration" page (e.g. `https://otomarket.jp/api/license/v1`). POST with `Content-Type: application/json`.

- `POST {baseUrl}/activate` — activate the license on a device (uses one seat)
- `POST {baseUrl}/verify` — check status (re-fetch the signed license)
- `POST {baseUrl}/deactivate` — release the license on a device (frees the seat)
- `GET {baseUrl}/keys` — fetch the public signing keyset (JWKS) for verification (see below)

### `GET {baseUrl}/keys` (public keyset)

To support per-creator signing keys, a token header may carry a `kid` (key id).
This endpoint returns the **public keys only** (private keys are never returned).
No auth required; GET.

```json
{ "keys": [ { "kid": "cr_<creatorId>_<rand>", "publicKeyPem": "-----BEGIN PUBLIC KEY-----\n..." } ] }
```

How verifiers use it: verify the signature with the key whose `kid` matches the
token. Tokens without a `kid` are verified with the single public key from
the product page. The official OtoMarket SDKs (JS/C++)
fetch, cache, and select keys automatically. In a custom implementation, fetch
this endpoint when you see a `kid` token and verify against the matching key
(cache the result and refresh on an unknown `kid`).

## Request fields

| Field | activate | verify / deactivate | Meaning |
| --- | --- | --- | --- |
| `productKey` | required | required | The product's public identifier (the productKey on the product edit page, = `Product.id`). `productId` also accepted |
| `licenseKey` | required | required | The buyer's license key (max 256 chars) |
| `machineId` | required | required | A stable, unique per-device id (max 256 chars). See below |
| `machineName` | optional | — | Display label (activate only, e.g. `Studio Mac`) |

### How to build `machineId` (important)

- Make it **stable and unique per device**. Send the same value every time for the same user + device (a value that changes each time wastes seats).
- Prefer a **hashed** value rather than sending a raw hardware id (privacy).
- Example: generate a random UUID on first launch, store it locally on the device, and use that UUID (hashed if you like) as the `machineId`.
- For testing, use a unique, recognizable value like `sandbox-yourname-macbook`; avoid generic values like `test-machine`.

## Responses

### Success (HTTP 200, `ok: true`)

```json
{ "ok": true, "license": "<header.payload.signature>", "seatsUsed": 1, "maxActivations": 5, "expiresAt": null }
```

- `license` is an **Ed25519-signed token** (`header.payload.signature`). Verify it **offline with the distributed signing public key**.
- In your own implementation, verify the signature with the public key and check that the payload's `productId` / `machineId` / `expiresAt` match your expectations before unlocking features.
- `expiresAt` of `null` means no expiry; a value means the license expires then.

### Business failure (HTTP 200, `ok: false`)

```json
{ "ok": false, "error": "SEAT_LIMIT" }
```

| `error` | Meaning |
| --- | --- |
| `NOT_FOUND` | License key does not exist / invalid |
| `PRODUCT_MISMATCH` | Key belongs to a different product (`productKey` mismatch) |
| `SEAT_LIMIT` | Reached the concurrent-device limit (deactivate another device) |
| `EXPIRED` | Expired |
| `REVOKED` | Revoked (e.g. refunded) |

### Request / limit errors (HTTP 4xx)

| HTTP | code | Meaning |
| --- | --- | --- |
| 400 | `INVALID_REQUEST_BODY` | Malformed JSON / missing required fields |
| 429 | `LICENSE_TEMPORARILY_LOCKED` | Too many failures for the same key (5/hour). Retry later |
| 429 | (rate limited) | Too many calls per IP (10/minute) |
| 404 | `NOT_FOUND` | License API not available (does not happen where it is offered) |

## Offline use

If you store the signed `license` returned by `verify`, you can **verify it offline** with the public key even when the API is temporarily down (the C++ SDK automates this within `verifyRetryGrace`). In your own implementation you can re-fetch with `verify` periodically and, when offline, decide based on the stored token's signature and `expiresAt`.

## curl example

A full, runnable example (with a sandbox key) is in the [quickstart](./quickstart.md).

## Public key handling

Ship only the **signing public key** used for verification. Never ship or display `LICENSE_SIGNING_PRIVATE_KEY`.
