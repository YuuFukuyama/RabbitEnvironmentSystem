#ifndef APP_CLOCK_H
#define APP_CLOCK_H

/* RTC 硬体の可否（10 分アラーム用）。 */

#include <stdint.h>

/* LSE と RTC が使えるときだけ 1。 */
void AppClock_SetHardwareReady(uint8_t ready);

/* RTC 硬体が使えるか。 */
uint8_t AppClock_HwReady(void);

#endif
