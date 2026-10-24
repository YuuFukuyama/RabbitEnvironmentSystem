/* RTC 暦を読み、中継同期済みか BKP で判定する。 */

#include "app_clock.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_rtc_ex.h"

extern RTC_HandleTypeDef hrtc;

#define APP_CLOCK_BKP_MAGIC 0xA5C1U

static uint8_t hw_ready;
static uint8_t boot_diag;
static uint8_t boot_snap_valid;
static uint16_t boot_snap_rx_got;
static uint16_t boot_snap_payload_len;
static uint8_t boot_snap_key_mask;
static uint8_t boot_snap_http_urc;

void AppClock_SetHardwareReady(uint8_t ready)
{
  hw_ready = ready;
}

void AppClock_SetBootDiag(uint8_t code)
{
  boot_diag = code;
}

uint8_t AppClock_GetBootDiag(void)
{
  return boot_diag;
}

void AppClock_SetBootSnap(uint16_t rx_got, uint16_t payload_len, uint8_t key_mask, uint8_t http_urc)
{
  boot_snap_valid = 1U;
  boot_snap_rx_got = rx_got;
  boot_snap_payload_len = payload_len;
  boot_snap_key_mask = key_mask;
  boot_snap_http_urc = http_urc;
}

uint8_t AppClock_BootSnapValid(void)
{
  return boot_snap_valid;
}

void AppClock_ReadBootSnap(uint16_t *rx_got, uint16_t *payload_len, uint8_t *key_mask, uint8_t *http_urc)
{
  if (rx_got != NULL)
  {
    *rx_got = boot_snap_rx_got;
  }
  if (payload_len != NULL)
  {
    *payload_len = boot_snap_payload_len;
  }
  if (key_mask != NULL)
  {
    *key_mask = boot_snap_key_mask;
  }
  if (http_urc != NULL)
  {
    *http_urc = boot_snap_http_urc;
  }
}

uint8_t AppClock_HwReady(void)
{
  return hw_ready;
}

void AppClock_MarkSynced(void)
{
  if ((hw_ready == 0U) || (hrtc.Instance != RTC))
  {
    return;
  }
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, APP_CLOCK_BKP_MAGIC);
  boot_snap_valid = 0U;
}

void AppClock_Read(uint8_t *valid, uint8_t *mon, uint8_t *day, uint8_t *hour, uint8_t *min)
{
  RTC_TimeTypeDef rtc_time;
  RTC_DateTypeDef rtc_date;

  if (valid != NULL)
  {
    *valid = 0U;
  }
  if ((hw_ready == 0U) || (hrtc.Instance != RTC))
  {
    return;
  }
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) != APP_CLOCK_BKP_MAGIC)
  {
    return;
  }
  if (HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
  {
    return;
  }
  if (HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
  {
    return;
  }
  if (valid != NULL)
  {
    *valid = 1U;
  }
  if (mon != NULL)
  {
    *mon = rtc_date.Month;
  }
  if (day != NULL)
  {
    *day = rtc_date.Date;
  }
  if (hour != NULL)
  {
    *hour = rtc_time.Hours;
  }
  if (min != NULL)
  {
    *min = rtc_time.Minutes;
  }
}
