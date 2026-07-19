<!-- lang-switch -->
**English** · [日本語](../ja/glossary.md)

# License Glossary (plain words)

A plain-language glossary for the license integration guides. No prior knowledge
assumed.

> Analogy: A **license** is like a **concert ticket**. **Activation** is
> **showing the ticket at the door**. A **seat** is **how many devices** the
> ticket lets in. **Offline grace** is the door **remembering you for a while**
> even if the ticket scanner loses internet.

---

## Core Terms

### License key

The code a buyer receives after purchase, for example `OTOMARKET-XXXX`. It
proves they bought the product. Like the ticket number.

### productKey

A public id that says **which product** a license is for. In OtoMarket it is
exactly `Product.id`. Safe to ship inside your app; it is not a secret.

### Activation

When the app sends the license key to OtoMarket and gets a "yes, this is valid"
answer. Like showing your ticket at the door the first time.

### Seat

How many devices one license may activate. "2 seats" means usable on 2 machines.
Reaching the limit returns `SEAT_LIMIT`.

### Expiry (`expiresAt`)

The date a license stops working. Perpetual (buy-once) licenses have no expiry;
subscription-type ones do.

### Revocation (`REVOKED`)

Turning a license off, for example after a refund. The next online check returns
`REVOKED`.

### machineId

A privacy-safe fingerprint of one device: a hash, not personal data. Lets the
system count seats without storing who you are.

---

## Security Terms

### Public key (`LICENSE_SIGNING_PUBLIC_KEY`)

Half of a key pair used to **check** that a license file is genuine. Safe to
embed in a shipped app. Goes into the SDK as `Config.publicKeyPem`.

### Private key (`LICENSE_SIGNING_PRIVATE_KEY`) — **never ship**

The other half, used by **OtoMarket's server only** to sign licenses. Never put
it in an app, doc, or repo.

### Signed license token

A small signed file the server returns on activation. The app can check it
**offline** with the public key, so it does not need the internet every time.

### Public keyset (keyset / `kid`)

OtoMarket may sign each creator's licenses with a different key, so that if one
key ever leaks the impact is limited to that creator's products. Such tokens
carry a `kid` (a key name tag), and the SDK fetches the required public keys from
`…/keys`. Only legacy tokens without `kid` use `publicKeyPem`; tokens with `kid`
require a matching keyset entry. A fetched cache works offline, but the first
fetch and the period immediately after key rotation require an online connection.

### Offline grace (`verifyAfter`)

A period where the app trusts the cached license without re-checking online.
Buy-once licenses use about 14 days; subscriptions use no more than one billing
period. The SDK also enforces a **maximum offline window (default ~30 days)**, so
it forces an online re-check at least that often (to limit abuse if a key leaks).
Normal use satisfies this automatically — no manual action by the user.

---

## Tooling Terms

### API (Layer 1 license API)

Web endpoints other programs call: `/api/license/v1/activate`, `/verify`,
`/deactivate`. A counter your app talks to over the internet.

### SDK

A ready-made code kit so you do not write the license logic yourself. Ours is
C++ at `packages/license-sdk-cpp`. Three functions do everything.

### The 3 functions

- `otoActivate(productKey, licenseKey, machineName)` — turn the license on here.
- `otoIsLicensed()` — ask "is this allowed right now?" Call it on every start.
- `otoDeactivate()` — free the seat.

### CMake / JUCE

CMake is the build tool that pulls the SDK into your project. JUCE is the common
C++ framework for audio plug-ins; we ship an optional JUCE adapter and a
ready-made activation screen.

### Sandbox

A safe test product and test key, so you can try activation without a real
purchase. `productKey=otomarket-sandbox-license-sdk`,
`licenseKey=OTOMARKET-SANDBOX-LICENSE-SDK`.

---

## Result Codes

| Code | Plain meaning |
| --- | --- |
| `SEAT_LIMIT` | Too many devices already. |
| `EXPIRED` | The license's date has passed. |
| `REVOKED` | The license was turned off, for example after a refund. |
| `NOT_FOUND` | No such license key. |
| `PRODUCT_MISMATCH` | Key is for a different product. |
| `INVALID_REQUEST_BODY` | The request was empty or malformed, which means the API is reachable. |
