<!-- lang-switch -->
**English** · [日本語](../ja/lv0.md)

# Lv0: Purchase Proof License Key

> Glossary: **[plain-words terms](./glossary.md)**.

**You'll know it worked when**: the buyer sees their key in the OtoMarket
library.

Lv0 is the default, zero-code option. OtoMarket issues a license key to the
buyer and shows it in the buyer library. The key works as purchase proof for
manual support, but the product itself does not enforce activation.

## What OtoMarket Provides

- A buyer-visible license key after purchase.
- Library storage so buyers can find their key later.
- Product ownership records for creator/admin support.

No SDK, API call, public key, or activation UI is required.

## Creator Steps

1. Keep **Issue license key** enabled for the product if the product should have
   a purchase proof key.
2. Tell buyers that their key is available in the OtoMarket library after
   purchase.
3. Use the key as purchase evidence when handling support manually.

Suggested buyer-facing copy:

> Your OtoMarket license key is available in your OtoMarket library after
> purchase. Keep it for support and purchase verification.

## Limitations

Lv0 does not block copying, sharing, or launching the product. Move to Lv1 or
Lv2 when the app or plug-in should check seats, expiry, and revocation.
