# 中継サーバー選定

## 1. 目的

本体と LINE Messaging API の間に置く中継の実行環境を、無料のまま継続できるものから選ぶ。

中継が行うことの前提は [03_basic_design.md](03_basic_design.md) と [04_detailed_design.md](04_detailed_design.md) である。HTTP のパスと本文、本体認証は [04_detailed_design.md](04_detailed_design.md) の 17.2 節で決めた。LINE 側の実装と、許可する LINE ユーザー ID の登録方法は [../relay/doc/setup.md](../relay/doc/setup.md) で決めた。

---

## 2. 中継が行うこと

- 公開 HTTPS で LINE の Webhook を受ける。署名を検証し、許可した LINE ユーザー ID の操作だけを保管する。同じ種類の未処理操作は最新 1 件にまとめる。
- 受け付けたらすぐに LINE へ「次の測定時（最大約 10 分後）に返す」と返す。測定結果と操作への返答は、本体が処理したあとに push する。
- 本体は約 10 分周期で未処理操作を取りに来る。未取得の操作は、次に本体が接続するまで中継が持つ。
- 電源投入またはリセットのあと、本体が時刻と通知モードをまだ受け取っていないあいだの操作取得の応答に、中継の現在時刻（日本時間）と、誰かが常に通知モードか（`notifyMode`）を載せる。時刻やモードのためだけのリクエストは増やさない。STOP からの復帰では載せる必要はない。LINE で通知設定を切り替えても、本体が次に起動するまで、生存確認の有無と送り先は変えない。返信では本体のリセットを案内する。
- 利用者ごとの通知設定（通常通知・安心・常に通知の3モード排他）は KV `NotifyUsers` の `notify-pref-by-user` に置く。`notify` が1の測定では、通常通知モードの利用者だけ push を11.4節どおり省略する。履歴は毎回残す。
- 届いた測定は毎回、KV の `History`（キー `measurements`）へ足す。1008 件を超えた分は古いものから捨てる。`notify` が 0 でも残す。
- チャネルアクセストークン、チャネルシークレット、本体との認証情報（Secret 名は `DEVICE_TOKEN`）は中継だけが持つ。値はリポジトリや公開資料には書かない。

本体側の HTTPS は ESP32-C3 の AT ファーム v3.3.0.0 である。ヘッダは `AT+HTTPCHEAD`、測定の本文は `AT+HTTPCPOST` に置く。これらのコマンドは 301 と 302 を追わない。

---

## 3. 必須条件

| 条件 | 理由 |
| --- | --- |
| 認証局が発行した証明書の公開 HTTPS | LINE の Webhook URL の条件 |
| Webhook に 2 秒以内で 2xx を返す | 超えると LINE は `request_timeout` とする |
| スリープしない | 起動に数秒かかると、上の 2 秒に合わない |
| 302 を返さない | AT の HTTP コマンドがリダイレクトを追わない |
| 未処理操作を、別のリクエストや再起動をまたいで保持する | 本体は約 10 分おきにしか取りに来ない |
| `api.line.me` へ HTTPS で送れる | 通知は中継が Messaging API を呼ぶ |
| 無料枠を超えても、有料プランへ上げない限り請求されない | 無料で継続するため |
| 秘密情報をソースコードに置かない | 詳細設計のセキュリティ |

---

## 4. 通信量

測定周期は約 10 分で、1 周期に操作の取得と測定結果の送信で 2 リクエストである。

| 項目 | 見積もり |
| --- | --- |
| 本体からのリクエスト | 1 日 144 周期 × 2 ＝ 288 |
| LINE の Webhook | 利用者の操作のときだけ |
| 未処理操作の書き込み | 利用者の操作のとき |
| 履歴の書き込み | 測定のたび。1 日 144。1008 件を超えた分は同じ値の中で古いものから捨てる |

この規模は、次節の採用先の無料枠に対して余裕がある。

---

## 5. 比較

| 候補 | 判定 | 理由 |
| --- | --- | --- |
| Cloudflare Workers + Workers KV | 採用 | 常時 HTTPS で、応答に 302 を挟まない。未処理操作は KV に置ける。無料枠の超過はエラーになり、有料プランへ上げない限り請求されない |
| Google Apps Script | 不採用 | Web アプリは POST に 302 を返す。`AT+HTTPCLIENT` はそれを追えない。コールドスタートが LINE の 2 秒を超えることがある |
| Deno Deploy | 不採用 | 無料枠の容量はこの通信量に足りる。超過したときの請求の境界が、Workers の「上限でエラー」より分かりにくい |
| スリープする無料 PaaS（Render の無料枠など） | 不採用 | 起動に数秒かかり、Webhook の 2 秒に合わない |
| Oracle Cloud の常時無料 VM | 不採用 | クレジットカード、OS の保守、インスタンスの空きに依存する |
| Firebase の Spark | 不採用 | Cloud Functions から `api.line.me` への通信には、請求先の登録が要る Blaze が必要 |

---

## 6. 採用

Cloudflare Workers と Workers KV を使う。URL は `*.workers.dev` とする。

無料プランの上限は次のとおり。数値は公式ドキュメントの記載による。変わった場合はこの資料を見直す。

| 項目 | 無料枠 |
| --- | --- |
| 受信リクエスト | 100,000 / 日。超過すると Error 1027 |
| CPU 時間 | 1 回 10ms。ネットワーク待ちは CPU 時間に入らない |
| 1 回の subrequest | 50。`fetch` と KV の操作が対象 |
| KV の読み取り | 100,000 / 日 |
| KV の書き込み | 1,000 / 日。同じキーは 1 秒に 1 回 |
| KV の保存 | 1GB。値は 1 キーあたり 25MiB まで |

未処理操作と測定履歴は KV に置く。履歴は 1008 件までで、超えた分は古い測定から捨てる。チャネルアクセストークン、チャネルシークレット、本体との認証情報（`DEVICE_TOKEN`）は Worker の secret に置き、リポジトリには書かない。値そのものは書かない。

通常の 1 周期は LINE への送信が数回で、subrequest の 50 に対して余裕がある。詳細ログを 1 件ずつ別の `fetch` に分けても、50 を超えない分割にする。

---

## 7. LINE の通数

中継の実行環境が無料であることと、LINE 公式アカウントのメッセージ通数は別である。

日本のコミュニケーションプランは月額 0 円で、無料メッセージは月 200 通である。上限を超えるとエラーになり、メッセージは送られない。このプランは追加メッセージを買えない。通数は送信対象の人数で数える。1 回のリクエストにメッセージを複数載せても、通数は増えない。

Messaging API で通数に入るのは push、マルチキャスト、ブロードキャスト、ナローキャストである。応答メッセージ（reply）は入らない。

生存確認は、起動時に常に通知モードだった利用者へ、60 分ごとに中継が push する。この周期の測定では本体の `notify` は 0 で、送り先は中継が決める。24 時間で OFF にはしない。常に通知のままなら、1 人あたり 1 日 24 通である。月 200 通の無料枠は、1 人を常に通知にし続けると約 8 日で尽きる。異常通知は許可済みユーザーと管理者への push で、安心・常に通知は省略しない。通常通知モードは中継が push を抑える。操作の返答も push なので、この通数に加わる。通知設定の返信は reply にでき、その 1 通は通数に入らない。定期通知の開始・停止は reply で本体のリセットを案内する。

---

## 8. 実装前の確認

- [ ] 搭載済みの AT v3.3.0.0 から `workers.dev` へ `AT+HTTPCPOST` で HTTPS できる（SNI と証明書）
- [ ] 署名検証と LINE への応答が、無料プランの CPU 10ms に収まる

---

## 9. この資料で繰り返さないこと

LINE 側の実装と、許可する LINE ユーザー ID の登録方法は [../relay/doc/setup.md](../relay/doc/setup.md) で決めた。

本体と中継の HTTP のパス、本文、本体認証は [04_detailed_design.md](04_detailed_design.md) の 17.2 節で決めた。Secret 名は `DEVICE_TOKEN` である。値はこの資料に書かない。

---

## 10. 参照

- [Workers の上限](https://developers.cloudflare.com/workers/platform/limits/)
- [Workers の料金](https://developers.cloudflare.com/workers/platform/pricing/)
- [Workers KV の上限](https://developers.cloudflare.com/kv/platform/limits/)
- [LINE Webhook の受信](https://developers.line.biz/ja/docs/messaging-api/receiving-messages/)
- [Webhook のエラー統計](https://developers.line.biz/ja/docs/messaging-api/check-webhook-error-statistics/)
- [Messaging API の料金](https://developers.line.biz/ja/docs/messaging-api/pricing/)
- [ESP-AT の HTTP コマンド](https://docs.espressif.com/projects/esp-at/en/latest/esp32c3/AT_Command_Set/HTTP_AT_Commands.html)
