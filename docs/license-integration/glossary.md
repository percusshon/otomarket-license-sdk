# License Glossary (plain words) / 用語集（やさしい言葉）

A plain-language glossary for the license integration guides. No prior knowledge
assumed. / ライセンス連携ガイド用の、前提知識なしで読める用語集です。

> Analogy / たとえ話：A **license** is like a **concert ticket**. **Activation**
> is **showing the ticket at the door**. A **seat** is **how many devices** the
> ticket lets in. **Offline grace** is the door **remembering you for a while**
> even if the ticket scanner loses internet.
> ／**ライセンス**＝**コンサートのチケット**。**アクティベーション（認証）**＝**入口でチケットを見せる**こと。**座席（台数）**＝そのチケットで**何台まで**入れるか。**オフライン猶予**＝チケット読取機がネットに繋がらなくても**しばらくは入れてくれる**こと。

---

## Core terms / 基本用語

### License key / ライセンスキー
- EN: The code a buyer receives after purchase (e.g. `OTOMARKET-XXXX`). It proves
  they bought the product. Like the ticket number.
- JA: 購入後に買い手が受け取るコード（例 `OTOMARKET-XXXX`）。買った証拠になります。チケット番号のようなもの。

### productKey
- EN: A public id that says **which product** a license is for. In OtoMarket it
  is exactly `Product.id`. Safe to ship inside your app — it is not a secret.
- JA: その license が**どの商品向けか**を示す公開ID。OtoMarket では `Product.id` そのもの。アプリに埋め込んでOK（秘密ではない）。

### Activation / アクティベーション（認証）
- EN: When the app sends the license key to OtoMarket and gets a "yes, this is
  valid" answer. Like showing your ticket at the door the first time.
- JA: アプリがライセンスキーを OtoMarket に送り「有効です」と返事をもらうこと。最初に入口でチケットを見せる動作。

### Seat / 台数（座席）
- EN: How many devices one license may activate. "2 seats" = usable on 2
  machines. Reaching the limit returns `SEAT_LIMIT`.
- JA: 1つのライセンスで認証できる端末数。「2台」なら2台で使える。上限に達すると `SEAT_LIMIT` が返る。

### Expiry / 期限（expiresAt）
- EN: The date a license stops working. Perpetual (buy-once) licenses have no
  expiry; subscription-type ones do.
- JA: ライセンスが切れる日。買い切りは期限なし、サブスク型は期限あり。

### Revocation / 失効（REVOKED）
- EN: Turning a license off (e.g. after a refund). The next online check returns
  `REVOKED`.
- JA: ライセンスを無効化すること（例：返金後）。次のオンライン確認で `REVOKED` が返る。

### machineId / マシンID
- EN: A privacy-safe fingerprint of one device (a hash, not personal data). Lets
  the system count seats without storing who you are.
- JA: 端末1台のプライバシー安全な識別子（ハッシュ。個人情報ではない）。誰かを記録せずに台数を数えるためのもの。

---

## Security terms / セキュリティ用語

### Public key / 公開鍵 (`LICENSE_SIGNING_PUBLIC_KEY`)
- EN: Half of a key pair used to **check** that a license file is genuine. Safe
  to embed in a shipped app. Goes into the SDK as `Config.publicKeyPem`.
- JA: ライセンスファイルが本物かを**検証する**ための鍵の片割れ。アプリに埋め込んで安全。SDK には `Config.publicKeyPem` として渡す。

### Private key / 秘密鍵 (`LICENSE_SIGNING_PRIVATE_KEY`) — **never ship**
- EN: The other half, used by **OtoMarket's server only** to sign licenses.
  Never put it in an app, doc, or repo.
- JA: もう片方。**OtoMarket のサーバーだけ**がライセンスに署名するのに使う。アプリ・doc・リポジトリに**絶対に入れない**。

### Signed license token / 署名付きライセンストークン
- EN: A small signed file the server returns on activation. The app can check it
  **offline** with the public key, so it does not need the internet every time.
- JA: 認証時にサーバーが返す署名付きの小さなファイル。アプリは公開鍵で**オフライン**検証できるので、毎回ネットに繋がなくてよい。

### Offline grace / オフライン猶予 (`verifyAfter`)
- EN: A period where the app trusts the cached license without re-checking
  online. Buy-once ~14 days; subscription ≤ one billing period.
- JA: キャッシュしたライセンスをオンライン再確認なしで信頼する期間。買い切り約14日、サブスクは課金周期以下。

---

## Tooling terms / 技術ツール用語

### API (Layer 1 license API) / API（層1ライセンスAPI）
- EN: Web endpoints other programs call: `/api/license/v1/activate`,
  `/verify`, `/deactivate`. "A counter your app talks to over the internet."
- JA: 他のプログラムが叩くWeb窓口：`/api/license/v1/activate`・`/verify`・`/deactivate`。「アプリがネット越しに話しかける受付」。

### SDK / SDK（組み込み部品）
- EN: A ready-made code kit so you don't write the license logic yourself. Ours
  is C++ at `packages/license-sdk-cpp`. Three functions do everything.
- JA: ライセンス処理を自分で書かずに済む、出来合いのコード部品。OtoMarket のは C++ で `packages/license-sdk-cpp`。3つの関数で完結。

### The 3 functions / 3つの関数
- `otoActivate(productKey, licenseKey, machineName)` — EN: turn the license on
  here. JA: ここで認証する。
- `otoIsLicensed()` — EN: "is this allowed right now?" call it on every start.
  JA: 「今使ってよい？」起動毎に呼ぶ。
- `otoDeactivate()` — EN: free the seat. JA: 座席を解放する。

### CMake / JUCE
- EN: CMake = the build tool that pulls the SDK into your project. JUCE = the
  common C++ framework for audio plug-ins; we ship an optional JUCE adapter and
  a ready-made activation screen.
- JA: CMake = SDK をプロジェクトに取り込むビルドツール。JUCE = オーディオプラグイン用の定番C++フレームワーク。任意の JUCE アダプタと既製のアクティベーション画面を同梱。

### Sandbox / サンドボックス
- EN: A safe test product + test key, so you can try activation without a real
  purchase. `productKey=otomarket-sandbox-license-sdk`,
  `licenseKey=OTOMARKET-SANDBOX-LICENSE-SDK`.
- JA: 実購入なしで認証を試せる、安全なテスト商品＋テストキー。`productKey=otomarket-sandbox-license-sdk`、`licenseKey=OTOMARKET-SANDBOX-LICENSE-SDK`。

---

## Result codes / 結果コード

| Code | EN (plain) | JA（やさしく） |
| --- | --- | --- |
| `SEAT_LIMIT` | Too many devices already. | 台数の上限に達した。 |
| `EXPIRED` | The license's date has passed. | 期限が過ぎた。 |
| `REVOKED` | The license was turned off (e.g. refund). | 失効した（返金等）。 |
| `NOT_FOUND` | No such license key. | そのキーが無い。 |
| `PRODUCT_MISMATCH` | Key is for a different product. | 別商品向けのキー。 |
| `INVALID_REQUEST_BODY` | The request was empty/malformed (the API is reachable). | リクエストが空/不正（＝APIには届いている合図）。 |
