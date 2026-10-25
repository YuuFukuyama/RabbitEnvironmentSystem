/* RTC 硬体が使えるかのフラグ。 */

#include "app_clock.h"

static uint8_t hw_ready;

void AppClock_SetHardwareReady(uint8_t ready)
{
  hw_ready = ready;
}

uint8_t AppClock_HwReady(void)
{
  return hw_ready;
}
