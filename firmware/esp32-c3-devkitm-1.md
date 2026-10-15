# ESP32-C3 開発ボード仕様メモ

ZYHARUKO の ESP32-C3 開発ボード（Amazon ASIN [B0D6KRZ1ZB](https://www.amazon.co.jp/dp/B0D6KRZ1ZB)）について、ファームウェアを書くときに参照する項目をまとめた。販売名は「ESP32-C3 開発ボード WiFi + 互換ボード ESP32-C3-MINI-1」。検索語は ESP32-C3-DevKitM-1 である。

商品ページの本文に、電源電圧、ピン番号、フラッシュ容量、UART の記載は無い。箇条書きは「ESP32C3 mini1」「処理が速く消費電力が低い」「ホームオートメーション」といった説明だけである。ピン名、USB、USB-UART、スイッチは商品写真のシルクと部品から読んだ。モジュールの電気的特性と起動ピンは [ESP32-C3-MINI-1 データシート v2.2](https://documentation.espressif.com/esp32-c3-mini-1_datasheet_en.html)（2026年5月6日）、開発ボードとしてのピン番号は [ESP32-C3-DevKitM-1 ユーザーガイド](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c3/esp32-c3-devkitm-1/user_guide.html) による。写真の並びは、この公式ボードの J1 / J3 と一致する。

公式ボードとの違いは、USB-UART が CH340C であることと、GPIO8 のアドレス指定 RGB LED が写真では確認できないことである。公式ボードのブリッジは最大 3 Mbps、RGB LED は GPIO8 駆動と案内されている。

## 概要

載っているモジュールは ESP32-C3-MINI-1。シールドに ESPRESSIF と ESP32-C3-MINI-1 の印字がある。アンテナはモジュール端の PCB アンテナで、外部アンテナ端子の MINI-1U ではない。コアはパッケージ内の ESP32-C3（32 bit RISC-V、最大 160 MHz）。

- ROM 384 KB、SRAM 400 KB（うち 16 KB はキャッシュ）、RTC SRAM 8 KB。
- フラッシュはチップ内蔵。MINI-1 の 4 MB 品がこの基板の想定で、AT ファームも 4 MB 用を書く。8 MB 品（FH8X）は同じ販売名では区別できない。実機の容量は書き込みツールのフラッシュ検出で確認する。
- 無線は 2.4 GHz の IEEE 802.11 b/g/n（2412〜2484 MHz、20 MHz / 40 MHz、1T1R）と Bluetooth 5 LE。アンテナは共用。この装置では Bluetooth は使わない。
- モジュール電源は 3.0〜3.6 V、代表 3.3 V。周囲温度の区分は −40〜85 ℃ 品と −40〜105 ℃ 品がある。写真からはどちらとも分からない。
- モジュール上の水晶は 40 MHz。GPIO は 3.3 V で、5 V トレラントではない。絶対最大の電源電圧は 3.6 V。
- 引き出せる GPIO は 15 本。フラッシュ用 SPI はモジュール内で、ピンヘッダには出ていない。

基板上のコネクタは Micro-USB。スイッチは BOOT と EN。EN はモジュールの EN（CHIP_PU）で、公式ユーザーガイドの RST と同じく L でチップが止まる。USB とモジュール UART0 の間に CH340C がある。

## 販売ページの記載

ページが仕様として挙げているのは次だけである。

| 項目 | ページの記載 |
| --- | --- |
| 品名 | ESP32-C3 開発ボード。Wi-Fi 付きの互換ボード。モジュールは ESP32-C3-MINI-1 |
| ブランド | ZYHARUKO |
| 用途 | 開発。ホームオートメーションなど、接続を使う応用 |

数値の仕様表は無い。以降の電圧、電流、ピン機能はデータシートとユーザーガイド、商品写真による。

## 外形とピンの向き

部品面を見て、左が Micro-USB、右がモジュールの PCB アンテナである。ピンヘッダは長辺の両側に 15 ピンずつ。ピッチは 2.54 mm で、ブレッドボードに差さる。

TX / RX のシルクがある列が J3、5V と 3V3 のシルクがある列が J1 である。番号は公式 DevKitM-1 と同じで、J1 の 1 番は GND（3V3 側）、15 番は GND（5V 側）。J3 の 1 番は GND（TX 側）、15 番は GND（IO19 側）。リセットのシルクは EN で、公式の RST である。

アンテナは基板の端から外へ出ている。アンテナの近くにベタや金属を置かない。

## ピン一覧

種別は P が電源、I が入力、O が出力、T がハイインピーダンス。兼用機能は公式ユーザーガイドの表である。

### J1

| 番号 | シルク | 種別 | 機能 |
| --- | --- | --- | --- |
| 1 | GND | G | グランド |
| 2 | 3V3 | P | 3.3 V。オンボード LDO の出力 |
| 3 | 3V3 | P | 2 番と同じ |
| 4 | 2 | I/O/T | GPIO2、ADC1_CH2、FSPIQ。ストラップピン |
| 5 | 3 | I/O/T | GPIO3、ADC1_CH3 |
| 6 | GND | G | グランド |
| 7 | EN | I | CHIP_PU。L でチップ停止、H で動作。開放にしない |
| 8 | GND | G | グランド |
| 9 | 0 | I/O/T | GPIO0、ADC1_CH0、XTAL_32K_P |
| 10 | 1 | I/O/T | GPIO1、ADC1_CH1、XTAL_32K_N |
| 11 | 10 | I/O/T | GPIO10、FSPICS0 |
| 12 | GND | G | グランド |
| 13 | 5V | P | 5 V |
| 14 | 5V | P | 13 番と同じ |
| 15 | GND | G | グランド |

### J3

| 番号 | シルク | 種別 | 機能 |
| --- | --- | --- | --- |
| 1 | GND | G | グランド |
| 2 | TX | I/O/T | GPIO21、U0TXD。ログと書き込み。CH340C と共用 |
| 3 | RX | I/O/T | GPIO20、U0RXD。ログと書き込み。CH340C と共用 |
| 4 | GND | G | グランド |
| 5 | 9 | I/O/T | GPIO9。ストラップピン。BOOT スイッチ |
| 6 | 8 | I/O/T | GPIO8。ストラップピン。公式ボードでは RGB LED |
| 7 | GND | G | グランド |
| 8 | 7 | I/O/T | GPIO7、FSPID、MTDO。AT の UART1 TX |
| 9 | 6 | I/O/T | GPIO6、FSPICLK、MTCK。AT の UART1 RX |
| 10 | 5 | I/O/T | GPIO5、ADC2_CH0、FSPIWP、MTDI。AT の UART1 CTS |
| 11 | 4 | I/O/T | GPIO4、ADC1_CH4、FSPIHD、MTMS。AT の UART1 RTS |
| 12 | GND | G | グランド |
| 13 | 18 | I/O/T | GPIO18、USB_D− |
| 14 | 19 | I/O/T | GPIO19、USB_D+ |
| 15 | GND | G | グランド |

シルクの TX はモジュールの送信（GPIO21）である。STM32 の TX をこの TX へつなぐと送信同士になる。AT 通信では使わない。

## 電源

モジュールの推奨電源は 3.3 V（3.0〜3.6 V）。基板は 5 V を LDO で 3.3 V にする。給電は次のどれか一つにする。公式ガイドでも同時使用はしない。

- Micro-USB の 5 V。書き込みとログを見るときの既定
- ピンヘッダのシルク 5V と GND。TX / RX の列（J3）ではなく、反対側の列（J1）の 13 番と 14 番である。15 番は GND。Micro-USB の 5 V と同じレールである
- 3V3 ピンと GND。J1 の 2 番と 3 番で、LDO の出力である

外部電源がモジュールへ供給できる電流は 0.5 A 以上（データシートの I_VDD）。Wi-Fi 送信のピークは、3.3 V、25 ℃、デューティ 100% で次のとおり。

| 条件 | ピーク |
| --- | --- |
| 802.11b、1 Mbps、20.5 dBm | 350 mA |
| 802.11g、54 Mbps、18 dBm | 295 mA |
| 802.11n HT20、MCS7、17.5 dBm | 290 mA |
| 受信 802.11b/g/n HT20 | 82 mA |

STBee Mini の 3.3 V 出力で、この基板の 3V3 ピンから無線まで給電しない。ピークが小さいレギュレータの定格を超える。組み込み時の電源入力は J1 のシルク 5V（13 番または 14 番）で、STBee Mini の 5 V をここへ入れ、GND を STM32 と共通にする。USB ケーブルを挿したまま、このピンへ STBee Mini の 5 V は入れない。3V3 ピン（J1 の 2 番と 3 番）は LDO の出力であり、STBee Mini の 3.3 V からは給電しない。STBee Mini の 5 V ピンへ 9〜12 V を入れる外部電源は、この 5V ピンへつないだままでは使わない。

## 起動ピン

リセット、または EN を L から H にした瞬間の GPIO2、GPIO8、GPIO9 で起動モードが決まる。ラッチされたあとは通常の GPIO になる。EN が H になってから、ストラップとして読まれるまでのホールドは 3 ms。この間はホストからレベルを変えない。

リセット時の内部状態は、GPIO9 が弱いプルアップで 1、GPIO2 と GPIO8 はフローティング。データシートの起動表は次のとおり。太字に相当する既定は GPIO9 = 1 の SPI ブート。

| モード | GPIO2 | GPIO8 | GPIO9 |
| --- | --- | --- | --- |
| SPI ブート（通常） | 1 | 任意 | 1 |
| ダウンロード | 1 | 1 | 0 |

BOOT は GPIO9 を L にする。ダウンロードに入るときは BOOT を押したまま EN を押し、EN を離してから BOOT を離す。GPIO8 は ROM メッセージの出力にも使う。リセットのあいだ GPIO8 を L に固定しない。

GPIO9 は起動後も BOOT スイッチで L になる。通常動作の信号線に使わない。

## UART の役割

ESP-AT は UART を二つ使う。どちらも既定は 115200 baud、8 ビット、パリティなし、ストップ 1、フロー制御なし。

| 用途 | ポート | 基板のピン | 相手 |
| --- | --- | --- | --- |
| 書き込みとログ | UART0 | RX = GPIO20、TX = GPIO21 | PC。基板上で CH340C につながっている |
| AT コマンドと応答 | UART1 | RX = GPIO6、TX = GPIO7 | STM32、または確認用の USB-UART |

CTS は GPIO5、RTS は GPIO4。フロー制御を使わないときも、初期ファームはこの 2 本を AT ポートに割り当てる。他の信号に使わない。ハードウェアフロー制御が要るときだけ、相手の RTS / CTS と交差させる。

STM32 との接続は次のとおり。論理レベルは両方 3.3 V。

| STM32 | ESP32-C3 基板 |
| --- | --- |
| USART1_TX PA9（STBee 31 番） | J3 の 9 番（GPIO6、UART1 RX） |
| USART1_RX PA10（STBee 32 番） | J3 の 8 番（GPIO7、UART1 TX） |
| GND | GND |

シルクの TX / RX は UART0 なので、STM32 の AT 用にはつながない。CH340C と信号が衝突する。STM32 側は USART1 の PA9 / PA10 である。配線は [usart.md](usart.md)。

## AT ファームウェアの導入

出荷時のファームが ESP-AT とは限らない。ESP32-C3-MINI-1 用の公式 AT を書き込んでから使う。WROOM 用やチップ名だけのバイナリは、ピン割り当てとフラッシュ容量が違うので使わない。

手順の正本は [Hardware Connection](https://docs.espressif.com/projects/esp-at/en/latest/esp32c3/Get_Started/Hardware_connection.html) と [Downloading Guide](https://docs.espressif.com/projects/esp-at/en/latest/esp32c3/Get_Started/Downloading_guide.html)。バイナリの一覧は [Released Firmware](https://docs.espressif.com/projects/esp-at/en/latest/esp32c3/AT_Binary_Lists/esp_at_binaries.html)。2026年10月時点の推奨版は v4.2.0.0。量産用は一覧の版を開き、AT Firmware Application から MINI-1 用を取得する。zip が直接置かれている例は v4.1.1.0 の `ESP32-C3-MINI-1-AT-V4.1.1.0.zip`。試験だけなら、同じページが示す GitHub Actions の `esp32c3-mini-1-at` か、ESP-AT のローカルビルドでもよい。

展開したパッケージの `factory/factory_MINI-1_unfilled.bin` をアドレス `0x0` に書く。このファイルが無い版は `factory/factory_MINI-1.bin` を同じアドレスに書く。フラッシュは DIO、40 MHz、4 MB。`download.config` の例は次のとおり。

```text
--flash-mode dio --flash-freq 40m --flash-size 4MB
0x0 bootloader/bootloader.bin
0x8000 partition_table/partition-table.bin
0xd000 ota_data_initial.bin
0x1e000 at_customize.bin
0x1f000 customized_partitions/mfg_nvs.bin
0x60000 esp-at.bin
```

個別バイナリを書き分けるのは、パーティション単位の更新のときだけにする。初回は factory の 1 本でよい。

### Windows での書き込み

1. データ線のある Micro-USB で PC につなぐ。充電専用ケーブルでは COM ポートが出ない。
2. デバイスマネージャに USB-SERIAL CH340 の COM が出ることを確認する。出ないときは WCH の CH340 ドライバを入れる。
3. Espressif の Flash Download Tools を開く。chipType は `ESP32-C3`、workMode は Developer Mode、loadMode は `uart`。
4. `factory_MINI-1_unfilled.bin`（無ければ `factory_MINI-1.bin`）にチェックを入れ、アドレスを `0x0` にする。DoNotChgBin を選び、bin 内のフラッシュパラメータを使う。
5. 上の COM を選んで書き込む。自動リセットでダウンロードに入らないときは、BOOT を押したまま EN を押し、EN を離してから BOOT を離す。

コマンドで書くときは、COM 番号を実機に合わせる。

```text
esptool --chip auto --port COM5 --baud 115200 --before default-reset --after hard-reset write-flash -z --flash-mode dio --flash-freq 40m --flash-size 4MB 0x0 factory/factory_MINI-1_unfilled.bin
```

### 書き込めたかの確認

USB シリアルはログ用（UART0）である。AT の `OK` はここには返らない。

1. シリアル端末を USB の COM に開く。115200、8N1、フロー制御なし。
2. EN を押す。ログに `module_name: MINI-1` と `AT cmd port:uart1 tx:7 rx:6 cts:5 rts:4 baudrate:115200` が出れば、MINI-1 用 AT が起動している。
3. 3.3 V の USB-UART を、相手 TX を基板の 6、相手 RX を基板の 7、GND を GND につなぐ。115200、8N1、改行は CR LF、フロー制御なし。
4. `AT+GMR` を送る。応答の末尾が `OK` で、`Bin version` に `MINI-1` が含まれれば導入できている。

例（版の日付はファームによって変わる）:

```text
AT+GMR
AT version:3.3.0.0(3b13d04 - ESP32C3 - May  8 2024 08:21:54)
SDK version:v5.0.6-dirty
compile time(be332568):May  8 2024 08:51:33
Bin version:v3.3.0.0(MINI-1)

OK
```

`OK` が無いときは、先に USB 側の起動ログを見る。`ready` の前で止まっている場合は書き込み先か、ダウンロードモードの抜けを疑う。

## AT コマンドの形

コマンドセットの説明は [AT Command Set](https://docs.espressif.com/projects/esp-at/en/latest/esp32c3/AT_Command_Set/index.html)。STM32 が守る契約は次のとおり。

- 終端は CR LF（`\r\n`）。LF だけはコマンドとして受け付けない。
- 1 コマンドは 256 バイト以内。
- 文字列は二重引用符で囲む。文字列中の `,` `"` `\` は `\,` `\"` `\\` でエスケープする。
- 省略できる引数を飛ばし、その後ろに引数があるときは、カンマを残す。
- 応答の最後は `OK` か `ERROR`。送信系は `SEND OK` / `SEND FAIL` になることがある。
- コマンドの結果以外に、モジュールから自発で届く。扱うものは `ready`、`busy p...`、`WIFI CONNECTED`、`WIFI GOT IP`、`WIFI DISCONNECT`。`busy p...` のあいだは次のコマンドを送らない。
- ボーレートを変えてフラッシュへ残すコマンドは `AT+UART_DEF`。変えると STM32 側も同じ値にする。この基板では既定の 115200 のままにする。
- ログに `at-common: write dlen:0` が出たら、UART1 RX の配線、ボーレート、GND、AT 以外のバイトを疑う。

既定の MINI-1 用バイナリには Wi-Fi、TCP-IP、HTTP が含まれる。有効な集合は、使った版の Firmware Differences で確認する。ピンを UART1 の初期値から移すには、ファームの再ビルドが要る。`AT+UART_DEF` は今の AT ポートのボーレートとフロー制御を変えるもので、GPIO 番号は変えない。

この装置で使う入口は次のとおり。LINE Messaging API へのリクエスト本文と、STM32 が載せる測定値の並びは未決定である。

| 目的 | コマンド | 戻ってくる目安 |
| --- | --- | --- |
| 生存確認 | `AT` | `OK` |
| 版の確認 | `AT+GMR` | `Bin version` に `MINI-1`、末尾 `OK` |
| ステーションモード | `AT+CWMODE=1` | `OK` |
| AP へ接続 | `AT+CWJAP="<ssid>","<password>"` | `WIFI CONNECTED`、`WIFI GOT IP`、`OK` |
| 再接続を止める | `AT+CWRECONNCFG=0,0` | `OK` |
| AP から外れる | `AT+CWQAP` | `OK`。`WIFI DISCONNECT` が続くことがある |
| 切断の通知 | なし（自発） | `WIFI DISCONNECT` |
| HTTPS | TCP-IP の SSL、または HTTP コマンド | コマンドごとの `OK` / `ERROR` |

`AT+CWJAP` は引数によって接続情報をフラッシュへ残す。再接続間隔を省略すると v3.3.0.0 では 1 秒で戻り直す。周期の終わりに STM32 は `AT+CWRECONNCFG=0,0` を書いてから `AT+CWQAP` を送る。電源投入時の自動接続は `AT+CWAUTOCONN?` で確認する。パスワードをコマンドに書くため、ログ用 UART0 を常時 PC へ繋いだままにしない。

## このリポジトリでの割り当て

STM32 が測定と判定を行い、ESP32-C3 は AT ファームのまま Wi-Fi と HTTPS を担当する。測定値の項目、タイムアウト、再接続の待ちは詳細設計の未決定項目のままである。

- 給電は STBee Mini の 5 V を、J1 のシルク 5V（13 番または 14 番）へ入れる。GND は共通。I/O は 3.3 V。3V3 ピンへは給電しない。USB ケーブルは挿さない。
- STM32 の USART1_TX（PA9）を GPIO6、USART1_RX（PA10）を GPIO7 へつなぐ。配線は [usart.md](usart.md)。115200、8N1、CR LF、フロー制御なし。
- シルクの TX / RX（GPIO21 / GPIO20）は書き込みとログ専用。
- GPIO4、GPIO5、GPIO9 は AT の RTS / CTS と BOOT に残す。
- Bluetooth は使わない。
- センサー通信の失敗と同じで、Wi-Fi や AT の失敗では `Error_Handler()` に入らない。測定周期は続ける。
