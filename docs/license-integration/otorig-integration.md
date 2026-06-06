# OtoRig 結線指示書（N.1：ライセンスAPI ドッグフーディング）

対象: OtoRig リポジトリ／別セッション。OtoMarket 側の準備物と結線手順をまとめた**リファレンス**。
関連: `docs/license-verification-api-design.md`, `docs/license-integration/quickstart.md`, `packages/license-sdk-cpp/README.md`, `docs/roadmap.md`（N.1 / L.4）

> 目的（roadmap N.1・最優先）：OtoRig 本体を OtoMarket の**稼働中ライセンスAPI（staging）**へ実結線し、「作者自身が使っている動く実例」を作る。これが②（開発者向けライセンス基盤）の営業資料・チュートリアル・信頼の土台になる。

> 用語がわからないときは **[やさしい用語集](./glossary.md)**（たとえ話・日英）を参照。
> ／New to the terms? See the **[plain-words glossary](./glossary.md)** (analogies, EN/JA).

## やさしい説明（これは何をするのか）/ In plain words

- JA: OtoRig は **無料プレイヤー＋作成キット**。お金が動くのは「OtoRig Player の上で動くプラグインパック」。**ライセンスの番人は OtoRig Player（ホスト）自身**＝**(A) ホスト集中型**。OtoRig Player に SDK を**1回だけ**組み込めば、各パックのライセンスを**まとめて確認**できる（パック作者はライセンス実装不要）。本書は「OtoRig Player に SDK を組み込み、OtoMarket のライセンス受付（API）に繋ぐ」手順書。
- EN: OtoRig is a **free player + creation kit**. Money happens on **plugin packs
  that run inside OtoRig Player**. The **license gatekeeper is OtoRig Player (the
  host)** = **(A) host-centric**: integrate the SDK **once** in OtoRig Player and
  it checks **every pack's** license (pack creators write no license code). This
  doc wires OtoRig Player's SDK to OtoMarket's license API.

> 流れ（3行）/ The flow in 3 lines：
> 1. OtoRig Player に SDK を入れる（CMake）/ add the SDK to OtoRig Player (CMake)
> 2. 設定（受付URL・公開鍵）を渡す / pass config (API URL + public key)
> 3. 起動時に「使ってよい？」を聞く（`otoIsLicensed()`）/ ask "allowed?" on start

---

## 0. OtoMarket 側の準備（すべて完了済み）

- **層1 API**：`/api/license/v1/{activate,verify,deactivate}` が staging で稼働（`LICENSE_API_ENABLED=true`）。
- **C++ SDK**：`packages/license-sdk-cpp`（コア＋JUCE HTTPアダプタ＋埋め込みUI `LicenseActivationPanel`＋サンプル＋CI＋packaging、#201/#205）。
- **sandbox テスト資格情報**（実購入なしで検証可）：
  - productKey: `otomarket-sandbox-license-sdk`
  - licenseKey: `OTOMARKET-SANDBOX-LICENSE-SDK`
  - 注意：sandbox データは `npm run seed`（手動）で投入。**staging DB に未投入なら**、OtoMarket 側で sandbox 発行（`scripts/issue-license-sandbox-key.ts` 相当 or 限定seed）を実施する必要がある。まず疎通だけなら下記 curl で確認。
- **署名検証用の公開鍵**：staging の `LICENSE_SIGNING_PUBLIC_KEY`（**公開鍵＝OtoRig アプリに埋め込んで安全**）。OtoMarket 運用者から PEM を受け取り `Config.publicKeyPem` に注入。**秘密鍵 `LICENSE_SIGNING_PRIVATE_KEY` はサーバー専用・絶対に配布しない**。

> ⚠️ **依存（要マージ）**：現在 otomarket.jp は**クローズドベータ・ゲート**有効で、`/api/license/v1/*` が `401 BETA_GATE_LOCKED` を返す。**ベータゲートの allowlist に `/api/license` を追加する修正を別PRで対応中**。マージ後にライセンスAPIが外部から疎通可能になる（ライセンスAPI自体の保護は `LICENSE_API_ENABLED`＋レート制限で維持）。

---

## 1. OtoRig が注入する設定（Config）

| 項目 | 値 |
|---|---|
| `baseUrl` | `https://otomarket.jp/api/license/v1` |
| `publicKeyPem` | staging `LICENSE_SIGNING_PUBLIC_KEY` の PEM（運用者から受領・埋め込み可） |
| `cachePath` | OtoRig アプリ専用のキャッシュ先（`defaultCachePath("OtoRig")` 等） |
| `http` | `JuceHttpTransport`（SDK同梱） |
| `expectedProductId` | ドッグフーディングは sandbox の `otomarket-sandbox-license-sdk`／本番は OtoRig の Product.id |
| `machineId` | `deriveMachineId(productId, defaultMachineFingerprint())` |

**staging値はハードコードせず注入**（SDK設計どおり）。本番切替時は baseUrl/publicKey/productId を差し替えるだけ。

---

## 2. CMake 取り込み（OtoRig 側）

`packages/license-sdk-cpp/README.md` の3方式いずれか（`add_subdirectory` / `FetchContent_Declare` / `find_package`）。JUCEアダプタと埋め込みパネルを有効化：

```
set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON CACHE BOOL "")
set(OTOMARKET_LICENSE_SDK_BUILD_ACTIVATION_PANEL ON CACHE BOOL "")
```

リンク：`otomarket::license_sdk`（コア）＋ `otomarket::license_sdk_juce`／`otomarket::license_sdk_activation_panel`（任意）。

---

## 3. 結線（OtoRig Phase4 オンライン照会）

1. 起動時に `Client`（Config注入）を構築。
2. 既存「Phase4 オンライン照会」を SDK の3関数へ置換：
   - 認証：`client.otoActivate(productId, licenseKey, machineName)`。
   - 起動毎の判定：`client.otoIsLicensed()`（キャッシュ署名のオフライン検証＋`verifyAfter`到来時はオンライン再照会、失敗でも猶予内なら true）。
   - 解除：`client.otoDeactivate()`。
3. UI（Lv2）：`LicenseActivationPanel` を設定画面に `addAndMakeVisible` で設置（キー入力→activate→状態/座席/期限表示→deactivate）。
4. ネットワークは UI 非ブロック（パネルは非同期実装済み）。

---

## 4. 疎通スモークテスト（curl・ベータゲート allowlist マージ後）

```
# verify（sandbox 資格情報・staging）
curl -s -X POST https://otomarket.jp/api/license/v1/verify \
  -H "Content-Type: application/json" \
  -d '{"productId":"otomarket-sandbox-license-sdk","licenseKey":"OTOMARKET-SANDBOX-LICENSE-SDK","machineId":"<hashed-machine-id>"}'
```

- 期待：`LICENSE_API_ENABLED` 有効＋sandbox データ存在で `{ ok, status, ... }`。`{}` だけ送ると `400 INVALID_REQUEST_BODY`（＝API到達の合図）。
- `401 BETA_GATE_LOCKED` が返る間は allowlist 修正が未マージ。

---

## 5. 受け入れ基準（N.1 完了の定義）

- OtoRig が staging API に activate→verify→deactivate を実行でき、`LicenseActivationPanel` で状態が見える。
- オフライン猶予（`verifyAfter`）でネット断でも一定期間 licensed。
- このフローを記事化（「OtoRig にライセンスを数行で入れた」）＝ N.2/N.3 の営業資料に転用。

> 本書は OtoMarket 側リファレンス。実装は OtoRig リポジトリ／別セッションで行う（このセッションは OtoMarket 担当）。
