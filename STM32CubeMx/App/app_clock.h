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

/* LSE と RTC が使えるときだけ 1。 */
void AppClock_SetHardwareReady(uint8_t ready);

/* 中継から暦を書けたあとに呼ぶ。 */
void AppClock_MarkSynced(void);

/* 起動時同期の失敗理由。LCD のプレースホルダに使う。 */
void AppClock_SetBootDiag(uint8_t code);

/* 直近の BootDiag を返す。 */
uint8_t AppClock_GetBootDiag(void);

/* RTC 硬体が使えるか。 */
uint8_t AppClock_HwReady(void);

/* 同期済みなら valid=1 で月日時分を返す。 */
void AppClock_Read(uint8_t *valid, uint8_t *mon, uint8_t *day, uint8_t *hour, uint8_t *min);

#endif
