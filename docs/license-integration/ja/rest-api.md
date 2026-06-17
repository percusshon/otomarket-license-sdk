<!-- lang-switch -->
[English](../en/rest-api.md) · **日本語**

# ライセンス REST API（どの言語でも）

> 用語集: **[やさしい用語集](./glossary.md)**。

C++/JUCE 以外のプラグインやアプリは、**この REST API を直接呼べばどの言語でも**ライセンス認証を組み込めます（C++ SDK もこの API を内部で呼んでいます）。

> **AI で組み込む**: このページと[クイックスタート](./quickstart.md)を Cursor / Claude / ChatGPT などの AI アシスタントに渡せば、お使いの言語の組み込みコードを生成してもらえます。まず[クイックスタート](./quickstart.md)の sandbox キーで疎通を確認してください。

## エンドポイント

ベース URL は商品の「ライセンス連携」ページに表示されます（例 `https://otomarket.jp/api/license/v1`）。`Content-Type: application/json` で POST します。

- `POST {baseUrl}/activate` — 端末でライセンスを有効化（座席を1つ使う）
- `POST {baseUrl}/verify` — 状態を確認（署名済みライセンスを取り直す）
- `POST {baseUrl}/deactivate` — 端末のライセンスを解除（座席を返す）
- `GET {baseUrl}/keys` — 署名検証用の公開鍵セット（JWKS）を取得（後述）

### `GET {baseUrl}/keys`（公開鍵セット）

クリエイターごとの署名鍵に対応するため、トークン header に `kid`（鍵 ID）が入る場合があります。このエンドポイントは検証に使う**公開鍵のみ**を返します（秘密鍵は決して返しません）。認証不要・GET。

```json
{ "keys": [ { "kid": "cr_<creatorId>_<rand>", "publicKeyPem": "-----BEGIN PUBLIC KEY-----\n..." } ] }
```

検証側の使い方：トークンの `kid` に一致する鍵で署名を検証します。`kid` の無いトークンは、商品ページの単一公開鍵で検証します。OtoMarket 公式 SDK（JS/C++）はこの取得・キャッシュ・選択を自動で行います。自前実装では、`kid` 付きトークンを受けたら本エンドポイントを取得し、`kid` 一致の鍵で検証してください（取得はキャッシュし、未知 `kid` のときに取り直すのが推奨）。

## リクエスト項目

| 項目 | activate | verify / deactivate | 内容 |
| --- | --- | --- | --- |
| `productKey` | 必須 | 必須 | 商品の公開識別子（商品編集ページの productKey、= `Product.id`）。`productId` でも可 |
| `licenseKey` | 必須 | 必須 | 購入者のライセンスキー（最大 256 文字） |
| `machineId` | 必須 | 必須 | 端末ごとに安定した一意の ID（最大 256 文字）。下記参照 |
| `machineName` | 任意 | — | 表示用ラベル（activate のみ。例 `Studio Mac`） |

### machineId の作り方（重要）

- **端末ごとに安定して一意**にします。同じユーザー・同じ端末では毎回同じ値を送ること（毎回変わると座席を無駄に消費します）。
- 生のハードウェア ID をそのまま送らず、**ハッシュ化した値**を推奨します（プライバシー）。
- 例: アプリ初回起動時にランダム UUID を生成して端末ローカルに保存し、その UUID（必要ならハッシュ化）を `machineId` として使う。
- テスト時は `sandbox-yourname-macbook` のように一意で分かる値を使い、`test-machine` のような汎用値は避けます。

## レスポンス

### 成功（HTTP 200, `ok: true`）

```json
{ "ok": true, "license": "<header.payload.signature>", "seatsUsed": 1, "maxActivations": 5, "expiresAt": null }
```

- `license` は **Ed25519 署名付きトークン**（`header.payload.signature`）。配布された**署名公開鍵でオフライン検証**できます。
- 自前実装では、公開鍵で署名を検証し、payload の `productId` / `machineId` / `expiresAt` が自分の想定と一致するかを確認してから機能を解放します。
- `expiresAt` が `null` なら無期限。値があればその日時で失効。

### ビジネス上の失敗（HTTP 200, `ok: false`）

```json
{ "ok": false, "error": "SEAT_LIMIT" }
```

| `error` | 意味 |
| --- | --- |
| `NOT_FOUND` | ライセンスキーが存在しない／無効 |
| `PRODUCT_MISMATCH` | キーが別商品のもの（`productKey` 不一致） |
| `SEAT_LIMIT` | 同時利用台数の上限に到達（別端末を deactivate する） |
| `EXPIRED` | 期限切れ |
| `REVOKED` | 失効済み（返金等） |

### リクエスト/制限エラー（HTTP 4xx）

| HTTP | code | 意味 |
| --- | --- | --- |
| 400 | `INVALID_REQUEST_BODY` | JSON が不正／必須項目が不足 |
| 429 | `LICENSE_TEMPORARILY_LOCKED` | 同一キーの失敗が多すぎる（5回/時）。一定時間後に再試行 |
| 429 | （レート制限） | IP あたりの呼び出しが多すぎる（10回/分） |
| 404 | `NOT_FOUND` | ライセンス API が未提供（提供環境では発生しません） |

## オフライン運用

`verify` で取得した署名済み `license` をローカルに保存しておけば、API が一時的に落ちていても公開鍵で**オフライン検証**できます（C++ SDK は `verifyRetryGrace` の範囲でこれを自動化します）。自前実装でも、定期的に `verify` で取り直しつつ、オフライン時は保存済みトークンの署名と `expiresAt` を見て判定する運用にできます。

## curl の例

実際に叩ける完全な例（sandbox キーつき）は[クイックスタート](./quickstart.md)にあります。

## 公開鍵の扱い

検証に使う**署名公開鍵のみ**を製品に同梱します。`LICENSE_SIGNING_PRIVATE_KEY` は絶対に同梱・表示しません。
