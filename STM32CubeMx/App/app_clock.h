#ifndef APP_CLOCK_H
#define APP_CLOCK_H

/* RTC 暦の読み出しと、中継同期済みかの判定。 */

#include <stdint.h>

/* LSE と RTC が使えるときだけ 1。 */
void AppClock_SetHardwareReady(uint8_t ready);

/* 中継から暦を書けたあとに呼ぶ。 */
void AppClock_MarkSynced(void);

/* 同期済みなら valid=1 で月日時分を返す。 */
void AppClock_Read(uint8_t *valid, uint8_t *mon, uint8_t *day, uint8_t *hour, uint8_t *min);

#endif
