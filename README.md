# ウサギ飼育環境の温湿度監視

家庭のケージを約 10 分ごとに測り、本体の LCD で状態を見せ、異常時は LINE へ知らせるシステム。個人で、要求定義からファームウェア、中継、テスト項目まで作っている。

2026年9月着手。11月からの実環境利用が目標で、測定系の実機確認まで進んでいる。

---

## 担当

| 項目 | 内容 |
| --- | --- |
| 形態 | 個人開発（1名） |
| 期間 | 2026年9月〜（進行中） |
| 工程 | 要求定義、基本設計、詳細設計、STM32 ファームウェア、中継、テスト仕様と実機確認 |
| 対象 | STM32F103CBT6（STBee Mini Ver.2）、GY-SHT31-D、ST7032i、ESP32-C3、Cloudflare Workers、LINE |

---

## システム構成

```text
SHT31 ──I2C── STM32F103 ──I2C── ST7032i LCD
                  │
                  │ USART1（AT）
                  ▼
              ESP32-C3 ──Wi-Fi / HTTPS── Cloudflare Workers
                                              │
                                              ▼
                                    LINE Messaging API
```

STM32 が測定、危険度判定、LCD、測定周期を持つ。ESP32-C3 は AT コマンドの Wi-Fi と HTTPS だけを担う。LINE のユーザーと許可、定期通知の ON/OFF、測定履歴は中継が持つ。

---

## 技術

| 区分 | 内容 |
| --- | --- |
| ファームウェア | C11、STM32Cube FW_F1 V1.8.7、HAL、CMake、GCC |
| マイコン | Cortex-M3、72 MHz（HSE 12 MHz × 6）。I2C1 100 kHz、USART1、RTC |
| センサー / 表示 | SHT31（単発測定、CRC-8）、ST7032（2行） |
| 待機 | RTC アラームで STOP。アラームを組めなければ `HAL_Delay` |
| 中継 | Cloudflare Workers、Workers KV、LINE Messaging API |
| 秘密情報 | SSID、パスワード、`DEVICE_TOKEN` は `wifi_secret.h`（Git 対象外）。LINE のシークレットは Workers の Secret |

アプリケーションの本体は `STM32CubeMx/App/` と `relay/src/index.js` にある。`STM32CubeMx/Drivers/` はベンダ提供の CMSIS / HAL である。

---

## 実装していること

- 10 分周期で温度・湿度を測り、0.1 単位の閾値とヒステリシスで危険度を決める。警告は連続検出のあと通知し、同じ異常の再通知は間隔を空ける。
- センサー通信に失敗しても `Error_Handler` には入らず、LCD に異常を出して次周期へ進む。
- 起動して最初に中継と通信できたときだけ、日本時間と定期通知の有無を受け取る。STOP からの復帰では取り直さない。生存確認は、その起動で ON を受け取っているあいだ 60 分ごと。
- 測定のたびに中継へ HTTPS POST し、送れたときだけ通知済みにする。送信後は AP から外す。Wi-Fi が切れても測定と LCD は続ける。
- 中継は LINE の署名を検証し、許可したユーザーだけに「いまの状態」「平均」「詳細」と定期通知の ON/OFF を返す。履歴は 1008 件まで残す。

中継のホストとトークン（`wifi_secret.h`）が無いビルドでは HTTPS 送信は行わない。温湿度の既定は実センサーで、ダミーシナリオは `ENV_SENSOR_USE_DUMMY` を付けたときだけ使う。

---

## 現状

| 範囲 | 状態 |
| --- | --- |
| 要求、基本設計、詳細設計、中継選定、テスト項目 | 文書化済み |
| SHT31 の測定、通信失敗時も周期が続くこと | 2026-10-15 に実機で合格（T-01〜T-03） |
| LCD、危険度、STOP、AT、中継クライアント、Workers | 実装済み。結合と実環境の合格記録はこれから |
| LINE「いまの状態」 | ダミー ON で 1 件合格（T-35）。本番の合格には数えない |
| カメラ、自動冷却、停電の直接検知 | 対象外。停電の疑いは生存確認の途絶で見る |

合否の記録は [doc/06_test.md](doc/06_test.md) の 10 章。

---

## ドキュメント

| 文書 | 内容 |
| --- | --- |
| [doc/01_requirements.md](doc/01_requirements.md) | 要求 |
| [doc/02_schedule.md](doc/02_schedule.md) | スケジュール |
| [doc/03_basic_design.md](doc/03_basic_design.md) | 基本設計 |
| [doc/04_detailed_design.md](doc/04_detailed_design.md) | 詳細設計 |
| [doc/05_relay_server.md](doc/05_relay_server.md) | 中継の選定 |
| [doc/06_test.md](doc/06_test.md) | テスト項目と実施記録 |
| [relay/doc/setup.md](relay/doc/setup.md) | Workers と LINE の構築 |
| [firmware/](firmware/) | 基板、SHT31、ST7032、ESP32-C3、I2C、USART |

---

## 要約

家庭で飼うウサギのケージ温湿度を、10分周期で監視するシステムを個人で設計・実装している。STM32F103（C11、HAL、CMake）が SHT31 を I2C で測り、ST7032 に表示し、閾値とヒステリシスで危険度を判定する。待ち時間は RTC アラームで STOP する。ESP32-C3 とは USART の AT コマンドで HTTPS し、Cloudflare Workers が LINE Messaging API へ通知する。起動時だけ時刻と生存確認の有無を受け取る。センサー故障や通信失敗でも測定周期は止めない。秘密情報はリポジトリに置かない。要求からテスト項目まで文書化している。2026年10月時点で、実センサーの測定と通信失敗時の継続は実機確認済み。LINE までの結合と連続稼働の確認は進行中。
