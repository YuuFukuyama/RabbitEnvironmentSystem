# USART

STM32F103CBT6 と ESP32-C3 の間は、USART を非同期（UART）で使う。ESP32-C3 側のピン、AT コマンド、給電の電流は [esp32-c3-devkitm-1.md](esp32-c3-devkitm-1.md)、STBee Mini のピン番号は [stbee-mini-v2.md](stbee-mini-v2.md) にある。測定値の並び、応答タイムアウト、再接続の待ちは詳細設計の未決定項目のままである。

## 信号

TX はその部品の送信、RX はその部品の受信である。相手の TX をこちらの RX へ、相手の RX をこちらの TX へつなぐ。

同期モードのクロック（CK）は出さない。CTS と RTS も使わない。フロー制御は無しである。

この型番にあるのは USART1、USART2、USART3 である。USART1 のクロックは APB2 の 72 MHz、USART2 と USART3 は APB1 の 36 MHz である。115200 baud はどちらでも出せる。

ピンを USART の TX または RX に割り当てると、そのピンは GPIO ではなく USART が駆動する。

## 配線

信号と電源は 4 本である。

| STBee Mini | ESP32-C3 基板 |
| --- | --- |
| USART TX | 6（GPIO6、UART1 RX） |
| USART RX | 7（GPIO7、UART1 TX） |
| 5V（2 番または 39 番） | 5V（J1 の 13 番または 14 番） |
| GND | GND |

論理レベルは 3.3 V。TX はプッシュプルで H も L も駆動するので、外部プルアップは付けない。5 V へプルアップすると、5 V を受けられない ESP32-C3 の GPIO を壊す。

給電は ESP32-C3 基板の 5V ピンへ入れる。基板上の LDO がモジュール用の 3.3 V を作る。STBee Mini の 3.3 V から ESP32-C3 の 3V3 ピンへは給電しない。Wi-Fi 送信のピークが、STBee Mini の 3.3 V 出力の定格を超える。USB ケーブルを挿したまま、5V ピンへ別の 5 V を入れない。

ESP32-C3 基板のシルク TX / RX（GPIO21 / GPIO20）は、PC への書き込みとログ用である。STM32 はここへつながない。CH340C と信号が衝突する。

## 使えるピン

番号は未決定である。TX と RX だけなら、いま空いている組み合わせは次の 3 組である。

| 周辺 | TX | RX | 基板ピン | 備考 |
| --- | --- | --- | --- | --- |
| USART1 | PA9 | PA10 | 31 番、32 番 | 5 V 入力可。クロックは 72 MHz |
| USART2 | PA2 | PA3 | 24 番、25 番 | 5 V 入力は不可。相手は 3.3 V |
| USART3 | PB10 | PB11 | 14 番、15 番 | 5 V 入力可。クロックは 36 MHz |

使わない兼用は次のとおり。

- USART1 の別ピン PB6 / PB7 は I2C1 の SCL / SDA である。
- USART1_CK は PA8（30 番）。同期クロックは出さないので、GPIO のまま空ける。
- USART1 の CTS / RTS（PA11 / PA12）は USB の D− / D+ と兼用である。
- PA13 / PA14 は SWD のまま残す。

## 通信条件

ESP-AT の UART1 に合わせる。

- 115200 baud、8 ビット、パリティなし、ストップ 1。
- 行末は CR LF（`\r\n`）。
- フロー制御なし。
- ボーレートは `AT+UART_DEF` で変えない。115200 のままにする。

## このリポジトリでの割り当て

ファームウェアは USART をまだ初期化していない。CubeMX の設定にも USART は無い。

- 測定と LCD 更新の周期は、USART の成否で変えない。
- AT や Wi-Fi の失敗では `Error_Handler()` に入らない。センサー通信の失敗と同じである。
