#ifndef APP_CLOCK_H
#define APP_CLOCK_H

/* RTC 暦の読み出しと、中継同期済みかの判定。 */

#include <stdint.h>

#define APP_CLOCK_BOOT_DIAG_NONE  0U
#define APP_CLOCK_BOOT_DIAG_HTTP  1U
#define APP_CLOCK_BOOT_DIAG_PARSE 2U
#define APP_CLOCK_BOOT_DIAG_NO_HW 3U
#define APP_CLOCK_BOOT_DIAG_RTC   4U
#define APP_CLOCK_BOOT_DIAG_AUTH  5U
#define APP_CLOCK_BOOT_DIAG_EMPTY 6U
#define APP_CLOCK_BOOT_DIAG_HTTPCL 7U
#define APP_CLOCK_BOOT_DIAG_NOTIM 8U
#define APP_CLOCK_BOOT_DIAG_GZIP  9U

#define APP_CLOCK_BOOT_SNAP_YEAR   0x01U
#define APP_CLOCK_BOOT_SNAP_MONTH  0x02U
#define APP_CLOCK_BOOT_SNAP_DAY    0x04U
#define APP_CLOCK_BOOT_SNAP_HOUR   0x08U
#define APP_CLOCK_BOOT_SNAP_MINUTE 0x10U

#define APP_CLOCK_BOOT_URC_NONE    0U
#define APP_CLOCK_BOOT_URC_CLIENT  1U
#define APP_CLOCK_BOOT_URC_GET     2U

/* LSE と RTC が使えるときだけ 1。 */
void AppClock_SetHardwareReady(uint8_t ready);

/* 中継から暦を書けたあとに呼ぶ。 */
void AppClock_MarkSynced(void);

/* 起動時同期の失敗理由。LCD のプレースホルダに使う。 */
void AppClock_SetBootDiag(uint8_t code);

/* 直近の BootDiag を返す。 */
uint8_t AppClock_GetBootDiag(void);

/* boot GET 直後の UART スナップショット。LCD 診断用。 */
void AppClock_SetBootSnap(uint16_t rx_got, uint16_t payload_len, uint8_t key_mask, uint8_t http_urc);

/* スナップショットが有効なら 1。 */
uint8_t AppClock_BootSnapValid(void);

/* スナップショットを読む。 */
void AppClock_ReadBootSnap(uint16_t *rx_got, uint16_t *payload_len, uint8_t *key_mask, uint8_t *http_urc);

/* RTC 硬体が使えるか。 */
uint8_t AppClock_HwReady(void);

/* 同期済みなら valid=1 で月日時分を返す。 */
void AppClock_Read(uint8_t *valid, uint8_t *mon, uint8_t *day, uint8_t *hour, uint8_t *min);

#endif
