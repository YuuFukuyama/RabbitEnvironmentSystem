/* RTC 暦を読み、中継同期済みか BKP で判定する。 */

#include "app_clock.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_rtc_ex.h"
#include <string.h>

extern RTC_HandleTypeDef hrtc;

#define APP_CLOCK_BKP_MAGIC 0xA5C1U

static uint8_t hw_ready;
static uint8_t boot_diag;
static uint8_t boot_snap_valid;
static uint16_t boot_snap_rx_got;
static uint16_t boot_snap_payload_len;
static uint8_t boot_snap_key_mask;
static uint8_t boot_snap_http_urc;
static uint8_t boot_fetch_parsed_ok;
static uint8_t boot_fetch_rtc_ok;
static uint16_t boot_fetch_year;
static uint8_t boot_fetch_month;
static uint8_t boot_fetch_day;
static uint8_t boot_fetch_hour;
static uint8_t boot_fetch_minute;
static uint8_t boot_fetch_notify_mode;
static uint16_t boot_fetch_rx_got;
static uint16_t boot_fetch_payload_len;
static char boot_fetch_preview[17];

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

void AppClock_SetBootFetch(uint8_t parsed_ok, uint8_t rtc_ok, uint16_t year, uint8_t month,
                           uint8_t day, uint8_t hour, uint8_t minute, uint8_t notify_mode,
                           uint16_t rx_got, uint16_t payload_len, const char *csv_preview)
{
  boot_fetch_parsed_ok = parsed_ok;
  boot_fetch_rtc_ok = rtc_ok;
  boot_fetch_year = year;
  boot_fetch_month = month;
  boot_fetch_day = day;
  boot_fetch_hour = hour;
  boot_fetch_minute = minute;
  boot_fetch_notify_mode = notify_mode;
  boot_fetch_rx_got = rx_got;
  boot_fetch_payload_len = payload_len;
  boot_fetch_preview[0] = '\0';
  if (csv_preview != NULL)
  {
    (void)strncpy(boot_fetch_preview, csv_preview, sizeof(boot_fetch_preview) - 1U);
    boot_fetch_preview[sizeof(boot_fetch_preview) - 1U] = '\0';
  }
}

void AppClock_ReadBootFetch(uint8_t *parsed_ok, uint8_t *rtc_ok, uint16_t *year, uint8_t *month,
                            uint8_t *day, uint8_t *hour, uint8_t *minute, uint8_t *notify_mode,
                            uint16_t *rx_got, uint16_t *payload_len, char *csv_preview,
                            uint8_t preview_cap)
{
  if (parsed_ok != NULL)
  {
    *parsed_ok = boot_fetch_parsed_ok;
  }
  if (rtc_ok != NULL)
  {
    *rtc_ok = boot_fetch_rtc_ok;
  }
  if (year != NULL)
  {
    *year = boot_fetch_year;
  }
  if (month != NULL)
  {
    *month = boot_fetch_month;
  }
  if (day != NULL)
  {
    *day = boot_fetch_day;
  }
  if (hour != NULL)
  {
    *hour = boot_fetch_hour;
  }
  if (minute != NULL)
  {
    *minute = boot_fetch_minute;
  }
  if (notify_mode != NULL)
  {
    *notify_mode = boot_fetch_notify_mode;
  }
  if (rx_got != NULL)
  {
    *rx_got = boot_fetch_rx_got;
  }
  if (payload_len != NULL)
  {
    *payload_len = boot_fetch_payload_len;
  }
  if ((csv_preview != NULL) && (preview_cap > 0U))
  {
    (void)strncpy(csv_preview, boot_fetch_preview, (size_t)preview_cap - 1U);
    csv_preview[preview_cap - 1U] = '\0';
  }
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
