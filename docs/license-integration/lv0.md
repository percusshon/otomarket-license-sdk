# Lv0: Purchase Proof License Key

> Glossary / 用語集: **[plain-words terms](./glossary.md)** (EN/JA).

**In plain words / やさしく言うと**: EN — the buyer just gets a key (like a
receipt number). No code, no enforcement. JA — 買い手にキー（領収書番号のようなもの）を渡すだけ。コード不要・強制なし。
**You'll know it worked when / 成功の合図**: EN — the buyer sees their key in
the OtoMarket library. JA — 買い手が OtoMarket ライブラリで自分のキーを見られる。

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

## Japanese Summary

Lv0 は購入証明キーのみの運用です。購入者ライブラリにライセンスキーを表示し、
サポート時の確認に使えます。アプリ側の強制認証は行いません。台数、期限、失効を
反映したい場合は Lv1/Lv2 を使います。
