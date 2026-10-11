/* 取得、判定、LCD 更新、起動時の操作取得、測定の送信を 1 回行う。 */

#include "app_cycle.h"
#include "at_cmd.h"
#include "env_judge.h"
#include "env_sensor.h"
#include "lcd_status.h"
#include "relay_client.h"

#define APP_NOTE_CAP 3U

void App_MeasureOnce(I2C_HandleTypeDef *hi2c, uint32_t now_sec)
{
  EnvReading reading;
  EnvView view;
  WifiNote notes[APP_NOTE_CAP];
  uint8_t note_count;
  uint8_t index;
  uint8_t wifi_failed;
  uint8_t wifi_up;
  uint32_t judge_sec;
  uint32_t synced_sec;

  reading.temp_deci_c = 0;
  reading.hum_deci_rh = 0;
  reading.ok = 0U;
  note_count = 0U;
  wifi_failed = 0U;
  wifi_up = 0U;
  judge_sec = now_sec;

  /* AT と Wi-Fi の確認。接続中だけアンテナを点ける。失敗しても測定は続ける。 */
  if (At_Probe() == HAL_OK)
  {
    (void)LcdStatus_ShowWifi(hi2c, 1U);
    wifi_up = 1U;
  }
  else
  {
    (void)LcdStatus_ShowWifi(hi2c, 0U);
  }

  if ((wifi_up != 0U) && (Relay_Enabled() != 0U))
  {
    if (Relay_TakeBoot(&synced_sec) != 0U)
    {
      EnvJudge_AcceptClock(now_sec, synced_sec);
      judge_sec = synced_sec;
    }
  }

  /* 通信失敗は 1 回だけ取り直す。ダミーの失敗行もこの 2 回で 1 件進む。 */
  if (EnvSensor_Read(&reading) != HAL_OK)
  {
    (void)EnvSensor_Read(&reading);
  }

  EnvJudge_Apply(&reading, judge_sec, &view, notes, &note_count, APP_NOTE_CAP);
  (void)LcdStatus_Show(hi2c, &view);

  /* 正常取得の前は 0 を測定値として送らない。 */
  if (EnvJudge_HasValue() == 0U)
  {
    EnvJudge_CommitSend(0U);
    return;
  }

  /* ダミー取得でも、中継の設定があれば測定を毎回送る。 */
  if (Relay_Enabled() != 0U)
  {
    if ((wifi_up == 0U) || (Relay_Post(notes, note_count) != HAL_OK))
    {
      wifi_failed = 1U;
    }
    EnvJudge_CommitSend((wifi_failed == 0U) ? 1U : 0U);
    EnvJudge_NoteWifi(wifi_failed);
    return;
  }

  for (index = 0U; index < note_count; index++)
  {
    if (Wifi_Publish(&notes[index]) != HAL_OK)
    {
      wifi_failed = 1U;
    }
  }
  EnvJudge_CommitSend((wifi_failed == 0U) ? 1U : 0U);
  if (note_count > 0U)
  {
    EnvJudge_NoteWifi(wifi_failed);
  }
}
