# 中継の再構築

## 1. 目的

LINE と Cloudflare Workers を、いま動いている状態から作り直す手順である。コードの正本は [../src/index.js](../src/index.js) である。

チャネルシークレット、チャネルアクセストークン、管理者のユーザー ID の値は、この資料とリポジトリに書かない。本体と中継の HTTP は、この資料では扱わない。

---

## 2. LINE

1. [LINE Developers](https://developers.line.biz/) でプロバイダーと Messaging API チャネルを作る。
2. Messaging API 設定で、長期のチャネルアクセストークンを発行する。チャネルシークレットも控える。
3. 応答メッセージとあいさつメッセージをオフにする。オンのままだと、Webhook の返信と LINE 側の自動応答が重なる。
4. 自分のスマホで、その公式アカウントを友だち追加する。

Webhook URL と Webhook の利用は、Workers の URL ができてから入れる。

---

## 3. Cloudflare Workers

Cloudflare のアカウントは1つで、その中の Workers を使う。Workers 専用の別アカウントはない。有料の Workers プランへは上げない。

1. [dash.cloudflare.com](https://dash.cloudflare.com/) で無料アカウントを作る。
2. **Workers & Pages** で **Create application** を押す。
3. **Workers** を選ぶ。Pages は使わない。
4. HTTP で文字を返す Hello World を選び、名前を付けてデプロイする。この STM32 のリポジトリは Git から取り込まない。
5. 付いた `https://….workers.dev/` を控える。

---

## 4. Secret

Worker の **Settings** → **Variables and Secrets** で、種類は Secret にして次を置く。`CHANNEL_SECRET` と `CHANNEL_ACCESS_TOKEN` は LINE のチャネル画面から取る。`ADMIN_USER_ID` は、あとで届く未許可の返信から取る。`DEVICE_TOKEN` は自分で決める。

| 名前 | 値 |
| --- | --- |
| `CHANNEL_SECRET` | チャネルシークレット |
| `CHANNEL_ACCESS_TOKEN` | 長期のチャネルアクセストークン |
| `ADMIN_USER_ID` | 管理者のユーザー ID |
| `DEVICE_TOKEN` | 本体が付ける共有トークン |

`ADMIN_USER_ID` は、未許可の返信の次の行に出る `U` で始まる 33 文字だけを入れる。前後の空白、改行、返信の文面は入れない。名前を `ALLOWED_USER_ID` にはしない。

Secret を保存したあとに出る **Deploy** を押す。コードだけ先に Deploy していると、その版に Secret が入らないことがある。

---

## 5. Workers KV

許可リスト、通知設定、測定履歴を、別々の名前空間に置く。

それぞれ、**Workers KV** で **Create instance** を押し、Webhook を受けている Worker の **Settings** → **Bindings** で **Add binding** する。種類は KV namespace。名前空間の名前と **Variable name** は同じにする。

| 名前空間 / Variable name | コード | 中のキー | 中身 |
| --- | --- | --- | --- |
| `LineUsers` | `env.LineUsers` | `allowed-user-ids` | 許可したユーザー ID |
| `NotifyUsers` | `env.NotifyUsers` | `notify-pref-by-user` など | 通知設定（11.4節）。旧 `notify-on-user-ids` は読取互換 |
| `History` | `env.History` | `measurements` | 測定履歴 |

名前空間を作っただけでは読めない。`allowed-user-ids` と `notify-on-user-ids` は Worker が書く。手では入れない。`measurements` は、このコードを Deploy して本体から測定が届くまで空のままである。

コードを Deploy したあとは、Binding が外れていないかをこの画面で見る。外れていれば、同じ内容でもう一度追加する。

---

## 6. コード

ダッシュボードの **Edit code** に [../src/index.js](../src/index.js) の内容を貼り、**Deploy** する。

ブラウザで Worker の URL を開く。画面が `ok` なら、このコードが本番に出ている。`Hello World!` のときは、貼り替えがまだデプロイされていない。

---

## 7. Webhook

1. LINE Developers の Messaging API 設定で、Webhook URL に Worker の `https://….workers.dev/` を入れる。
2. **Webhookの利用** をオンにする。
3. **検証** を押す。成功なら、署名検証まで通っている。

検証が 401 のときは、`CHANNEL_SECRET` がチャネルシークレットと一致していない。空白や改行も不一致になる。入れ直したあとは、もう一度 Deploy する。

Webhook の利用がオフだと、コンソールの検証は成功しても、スマホから送ったメッセージは Worker に届かない。

---

## 8. 動作確認

許可済みのユーザー（最初は `ADMIN_USER_ID` の本人）のトークで確認する。

履歴はまだ空である。測定を足す処理は 13 節まで入っていない。そのため「いまの状態」は未取得、「平均」と「詳細」は記録がない旨になる。

| 送る文字 | 返るもの |
| --- | --- |
| その他 | 「操作を選んでください。」と、平均・詳細のボタン |
| いまの状態 | 履歴の最新 1 件。履歴がなければ未取得。通知設定のモード名を付ける |
| 通知設定（旧「定期通知」） | 3 モードの説明と、通常通知・安心・常に通知のボタン。本体のリセット案内 |
| 通常通知モード、安心モード、常に通知モード | 選んだモードと、本体のリセット案内 |
| オン、オフ | 後方互換。常に通知／安心にマップ |
| 平均 | 直近 24 時間の平均。記録がなければその旨 |
| 詳細 | 直近 24 時間の記録を吹き出し 5 つまで。記録がなければその旨 |
| 上以外 | 「操作を選んでください。」 |

別の人を加えるときは、その人のスマホで公式アカウントへ何か1通送る。未許可なら、そのトークに自分のユーザー ID が返る。管理者のトークで、返ってきた ID だけを続けて送る。

```text
許可 Uxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

「許可しました。」と返ったあと、相手が「その他」を送って案内とボタンを受け取れば登録できている。許可した直後の1通目がまだ「未許可です」のときは、少し置いてからもう一度送る。外すときは、同じ ID を `許可解除` に続けて送る。管理者自身はリストに入れなくても操作できる。

---

## 9. リッチメニュー

LINE 側に登録済みである。Worker のコードでは作らない。作り直したときは、公式アカウントへ同じメニューを再度登録する。

半分の高さ（2500×843）で、横3列に次を置く。各ボタンは、8節の分岐と同じ文字をテキストで送る。

- いまの状態
- 通知設定（旧ラベル「定期通知」から差し替え）
- その他

---

## 10. 止まりやすい点

- Binding の Variable name が `LineUsers`、`NotifyUsers`、`History` と一致しないと、返信の前に Worker が止まり、トークには何も出ない。Logs に `reading 'get'` と出る。
- `isAllowed is not defined` と出るときは、イベント処理だけ差し替えて、許可リストの関数を入れていない。
- `ADMIN_USER_ID` が届いたユーザー ID と一致しないと、管理者でも「未許可です」になる。
- Webhook の利用がオフだと、検証だけ成功してトークのメッセージは来ない。
- コードが Hello World のままだと、検証は成功してもトークへ返信しない。

何も返らないときは、Worker の **Logs** を開始してから、もう一度送る。

---

## 11. サーバーが持つ情報

値そのものは書かない。

Secret は次のとおりである。

- `CHANNEL_SECRET`。Webhook の署名検証に使う。
- `CHANNEL_ACCESS_TOKEN`。LINE への返信に使う。
- `ADMIN_USER_ID`。許可の追加と解除ができる管理者。許可リストに入っていなくても操作できる。
- `DEVICE_TOKEN`。本体が `Authorization: Bearer` に付ける共有トークン。方式は [../../doc/04_detailed_design.md](../../doc/04_detailed_design.md) の 17.2 節。値は書かない。

KV の `LineUsers` が持つのは、許可したユーザー ID だけである。管理者の `許可` で追加し、`許可解除` で外す。キーは `allowed-user-ids` である。定期通知も履歴も、この名前空間には置かない。

通知設定は KV の `NotifyUsers` に置く。正本はキー `notify-pref-by-user` で、JSON オブジェクト `{ "Uxxxx": "standard" | "safe" | "always" }` である。1 利用者に 1 モードだけ。未設定は安心（`safe`）とする。表示名は通常通知モード、安心モード、常に通知モード。LINE の「通知設定」から切り替える。旧キー `notify-on-user-ids` が残っているときは、読み取り時に `always` へマップする。

通常通知（`standard`）は異常 push を中継が省略する。比較用に `last-alert-push-by-user` を同じ KV に置く。安心・常に通知は省略しない。常に通知だけが起動時 `notifyMode=1` と【定期通知】の送り先になる。定期の開始・停止は本体リセット後に反映する旨を返信する。通常通知への切替直後は省略比較をリセットする。

履歴は KV の `History` に置く。キーは `measurements` で、JSON の配列である。届いた測定を末尾へ足す。1008 件を超えた分は古いものから捨てる。1 件のプロパティは `at`、`temperature`、`humidity`、`temperatureState`、`humidityState`、`danger` である。`at` は中継が受けた日本時間で、ISO 8601 か `YYYY-MM-DD HH:mm:ss` である。`notify` と `periodic` は残さない。「いまの状態」は最新 1 件を返す。本体の次の測定は待たない。履歴がなければ未取得とする。平均と詳細は直近 24 時間である。欠測はそのまま件数に表す。詳細は、その 24 時間に届いた記録を、1 回の送信（吹き出し 5 つまで、1 人 1 通）で送る。収まりきらないときは、新しい記録を優先し、溢れた旨を 1 行入れる。

まだ本番へ出していないものは、次のとおりである。

`index.js` に、届いた測定を履歴へ足す処理と、起動時の操作取得がある。ダッシュボードへ貼って Deploy するまで、本番の `measurements` は空のままである。通信できなかった期間は欠測のままにする。日付と時間は、サーバーが届いた時刻を日本時間にしたものとし、STM32 の RTC は使わない。1008 件を超えた分は古い測定から捨て、1 つの値は 25MiB 未満に保つ。`LineUsers` への許可の追加が失敗しない空きを残す。認証済みの `GET /v1/ops` では、日本時間と `notifyMode` を返す。`notifyMode` が 1 のとき、その時点で常に通知モードのユーザー ID を次の起動まで【定期通知】の送り先として覚える。

持たないものは、次のとおりである。

- 通信できなかった測定の穴埋め。
- 利用者の ID や ON/OFF の一覧を、STM32 へ渡すこと。
- 日時設定。LINE の項目、コマンド、KV には置かない。起動時に返す現在時刻は、この KV には残さない。
- Wi-Fi の SSID とパスワード。

本体との認証は Secret `DEVICE_TOKEN` である。方式は [../../doc/04_detailed_design.md](../../doc/04_detailed_design.md) の 17.2 節である。値はリポジトリに書かない。

---

## 12. 残る課題

STM32 の回路やセンサーの未決定は [../../doc/04_detailed_design.md](../../doc/04_detailed_design.md) の32章に残す。ここには中継と LINE に関するものだけを書く。

- 生存確認は、起動時に常に通知モードだった利用者へ 60 分ごとに push する。24 時間で自動オフはしない。常に通知のままなら 1 人あたり 1 日 24 通で、月 200 通の無料枠は約 8 日で尽きる。異常通知と操作の返答も push で、この通数に加わる。通常通知モードは中継が push を抑える。通数の詳細は [../../doc/05_relay_server.md](../../doc/05_relay_server.md) の 7 節である。push の本文末尾には、取得できたときだけ `(今月 n/200)` を 1 行付ける。reply には付けない。
- 環境異常は、測定の `notify` が 1 のとき、許可済みユーザーと管理者へ送る。通常通知モードの利用者だけ省略する。操作の返答は、未処理操作に残す依頼者のユーザー ID へ push する。生存確認は、`periodic` が 1 で `notify` が 0 のとき、直近の起動時に常に通知だった利用者へ送る。送り先の判断は中継が行う。LINE で切り替えた送り先は、次のリセットまで使わない。
- 搭載済みの AT ファームから `workers.dev` へ `AT+HTTPCPOST` で HTTPS できるかは未確認である。署名検証と LINE への応答が無料プランの CPU 10ms に収まるかも未確認である。
- LINE 通信失敗時の再送回数と間隔、詳細ログを分割するときのメッセージサイズと送信間隔は未決定である。

---

## 13. 残る作業

- `index.js` の `/v1/ops` と `/v1/measurements` をダッシュボードへ貼って Deploy する。`DEVICE_TOKEN` を Secret に置く。本体の `RELAY_HOST` と `DEVICE_TOKEN` は `wifi_secret.h` に書き、リポジトリには入れない。
- 認証済み `GET /v1/ops` で `time` と `notifyMode` を返す。`notifyMode` が 1 のとき、その時点の常に通知モードのユーザー ID を次の起動まで送り先として覚える。
- 届いた測定を `History` の `measurements` に足す。1 件は `at`、`temperature`、`humidity`、`temperatureState`、`humidityState`、`danger` である。1008 件を超えた分は古い測定から捨てる。`notify` が 1 のときは許可済みユーザーと管理者へ push する（通常通知モードは省略あり）。`periodic` が 1 で `notify` が 0 のときは、起動時に常に通知だった利用者へ push する。ダミー取得でも同じ送信をする。
