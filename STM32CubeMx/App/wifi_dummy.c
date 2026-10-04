/* 通知を RAM に残すだけの Wi-Fi 送信。 */

#include "wifi_link.h"

#if !defined(WIFI_USE_DUMMY)
#error WIFI_USE_DUMMY を外すときは wifi_dummy.c を本番送信に差し替える
#endif

WifiNote wifi_dummy_log[WIFI_DUMMY_LOG_LEN];
uint8_t wifi_dummy_log_count;
/* 次に書く位置。 */
static uint8_t wifi_dummy_log_next;

HAL_StatusTypeDef Wifi_Publish(const WifiNote *note)
{
  if (note == NULL)
  {
    return HAL_ERROR;
  }

  wifi_dummy_log[wifi_dummy_log_next] = *note;
  wifi_dummy_log_next++;
  if (wifi_dummy_log_next >= WIFI_DUMMY_LOG_LEN)
  {
    wifi_dummy_log_next = 0U;
  }
  if (wifi_dummy_log_count < WIFI_DUMMY_LOG_LEN)
  {
    wifi_dummy_log_count++;
  }
  return HAL_OK;
}
