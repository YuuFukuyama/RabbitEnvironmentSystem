/* 起動時に通知モードを取り、測定のたびに中継へ送る。 */

#include "relay_client.h"
#include "at_cmd.h"
#include "env_judge.h"
#include "wifi_link.h"
#include "wifi_secret.h"
#include <string.h>

/* 起動後に通知モードを書けたら 1。 */
static uint8_t boot_done;

/* ホスト名として使えるかを返す。 */
static uint8_t host_ok(void)
{
  uint32_t index;

  if (RELAY_HOST[0] == '\0')
  {
    return 0U;
  }
  for (index = 0U; RELAY_HOST[index] != '\0'; index++)
  {
    if ((index >= 64U) || (RELAY_HOST[index] == '"') || (RELAY_HOST[index] == '/') ||
        (RELAY_HOST[index] == ' '))
    {
      return 0U;
    }
  }
  return 1U;
}

uint8_t Relay_Enabled(void)
{
  if ((host_ok() == 0U) || (DEVICE_TOKEN[0] == '\0'))
  {
    return 0U;
  }
  return 1U;
}

/* URL を https://ホストとパスで作る。 */
static uint8_t build_url(char *url, uint16_t cap, const char *path)
{
  uint16_t n;
  uint32_t index;
  const char *prefix;

  prefix = "https://";
  n = 0U;
  for (index = 0U; prefix[index] != '\0'; index++)
  {
    if ((n + 1U) >= cap)
    {
      return 0U;
    }
    url[n] = prefix[index];
    n++;
  }
  for (index = 0U; RELAY_HOST[index] != '\0'; index++)
  {
    if ((n + 1U) >= cap)
    {
      return 0U;
    }
    url[n] = RELAY_HOST[index];
    n++;
  }
  for (index = 0U; path[index] != '\0'; index++)
  {
    if ((n + 1U) >= cap)
    {
      return 0U;
    }
    url[n] = path[index];
    n++;
  }
  url[n] = '\0';
  return 1U;
}

uint8_t Relay_TakeBoot(uint32_t *synced_sec)
{
  const char *body;
  uint8_t mode;

  if ((boot_done != 0U) || (Relay_Enabled() == 0U) || (synced_sec == NULL))
  {
    return 0U;
  }
  if (At_HttpAuthBoot(DEVICE_TOKEN) != HAL_OK)
  {
    return 0U;
  }
  if (At_HttpGetBoot(RELAY_HOST, "/v1/ops") != HAL_OK)
  {
    return 0U;
  }
  body = At_HttpBootBody();
  if (At_BootParseNotifyMode(body, &mode) == 0U)
  {
    return 0U;
  }
  EnvJudge_SetNotifyMode(mode);
  boot_done = 1U;
  *synced_sec = 0U;
  return 0U;
}

/* JSON へ文字を足す。 */
static uint8_t json_char(char *json, uint16_t *n, uint16_t cap, char ch)
{
  if ((*n + 1U) >= cap)
  {
    return 0U;
  }
  json[*n] = ch;
  (*n)++;
  json[*n] = '\0';
  return 1U;
}

/* JSON へ文字列を足す。 */
static uint8_t json_text(char *json, uint16_t *n, uint16_t cap, const char *text)
{
  uint32_t index;

  for (index = 0U; text[index] != '\0'; index++)
  {
    if (json_char(json, n, cap, text[index]) == 0U)
    {
      return 0U;
    }
  }
  return 1U;
}

/* 0.1 単位を小数 1 桁で足す。 */
static uint8_t json_deci(char *json, uint16_t *n, uint16_t cap, int16_t deci)
{
  char tmp[8];
  uint8_t count;
  int16_t whole;
  int16_t frac;
  uint8_t negative;

  negative = 0U;
  if (deci < 0)
  {
    negative = 1U;
    deci = (int16_t)(-deci);
  }
  whole = (int16_t)(deci / 10);
  frac = (int16_t)(deci % 10);
  count = 0U;
  if (whole == 0)
  {
    tmp[count] = '0';
    count++;
  }
  while (whole > 0)
  {
    tmp[count] = (char)('0' + (whole % 10));
    count++;
    whole = (int16_t)(whole / 10);
  }
  if (negative != 0U)
  {
    if (json_char(json, n, cap, '-') == 0U)
    {
      return 0U;
    }
  }
  while (count > 0U)
  {
    count--;
    if (json_char(json, n, cap, tmp[count]) == 0U)
    {
      return 0U;
    }
  }
  if (json_char(json, n, cap, '.') == 0U)
  {
    return 0U;
  }
  return json_char(json, n, cap, (char)('0' + frac));
}

/* 危険度の名前。 */
static const char *risk_name(EnvRisk risk)
{
  if (risk == ENV_RISK_WARNING)
  {
    return "WARNING";
  }
  if (risk == ENV_RISK_DANGER)
  {
    return "DANGER";
  }
  if (risk == ENV_RISK_CRITICAL)
  {
    return "CRITICAL";
  }
  return "NORMAL";
}

/* 温度状態の名前。並びに TempState を合わせる。 */
static const char *temp_name(TempState temp_state)
{
  static const char *names[] = {"TEMP_FRZN", "TEMP_COLD", "TEMP_BEST", "TEMP_HIGH", "TEMP_CRIT"};

  if ((uint32_t)temp_state >= 5U)
  {
    return "TEMP_BEST";
  }
  return names[temp_state];
}

/* 湿度状態の名前。並びに HumState を合わせる。 */
static const char *hum_name(HumState hum_state)
{
  static const char *names[] = {"HUM_DRY", "HUM_LOW", "HUM_BEST", "HUM_HIGH", "HUM_WET"};

  if ((uint32_t)hum_state >= 5U)
  {
    return "HUM_BEST";
  }
  return names[hum_state];
}

/* 測定 JSON を作る。 */
static uint8_t build_json(char *json, uint16_t cap, const EnvReport *report)
{
  uint16_t n;

  n = 0U;
  json[0] = '\0';
  if (json_text(json, &n, cap, "{\"humidity\":") == 0U)
  {
    return 0U;
  }
  if (json_deci(json, &n, cap, report->hum_deci_rh) == 0U)
  {
    return 0U;
  }
  if (json_text(json, &n, cap, ",\"temperature\":") == 0U)
  {
    return 0U;
  }
  if (json_deci(json, &n, cap, report->temp_deci_c) == 0U)
  {
    return 0U;
  }
  if (json_text(json, &n, cap, ",\"humidityState\":\"") == 0U)
  {
    return 0U;
  }
  if (json_text(json, &n, cap, hum_name(report->hum_state)) == 0U)
  {
    return 0U;
  }
  if (json_text(json, &n, cap, "\",\"temperatureState\":\"") == 0U)
  {
    return 0U;
  }
  if (json_text(json, &n, cap, temp_name(report->temp_state)) == 0U)
  {
    return 0U;
  }
  if (json_text(json, &n, cap, "\",\"danger\":\"") == 0U)
  {
    return 0U;
  }
  if (json_text(json, &n, cap, risk_name(report->risk)) == 0U)
  {
    return 0U;
  }
  if (json_text(json, &n, cap, "\",\"notify\":") == 0U)
  {
    return 0U;
  }
  if (json_char(json, &n, cap, (report->notify != 0U) ? '1' : '0') == 0U)
  {
    return 0U;
  }
  if (json_text(json, &n, cap, ",\"periodic\":") == 0U)
  {
    return 0U;
  }
  if (json_char(json, &n, cap, (report->periodic != 0U) ? '1' : '0') == 0U)
  {
    return 0U;
  }
  return json_char(json, &n, cap, '}');
}

HAL_StatusTypeDef Relay_Post(const WifiNote *notes, uint8_t note_count)
{
  char url[128];
  char json[180];
  EnvReport report;
  const char *body;

  if (Relay_Enabled() == 0U)
  {
    return HAL_ERROR;
  }
  EnvJudge_CopyReport(notes, note_count, &report);
  if (build_json(json, sizeof(json), &report) == 0U)
  {
    return HAL_ERROR;
  }
  if (build_url(url, sizeof(url), "/v1/measurements") == 0U)
  {
    return HAL_ERROR;
  }
  if (At_HttpAuth(DEVICE_TOKEN) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (At_HttpPost(url, json) != HAL_OK)
  {
    return HAL_ERROR;
  }
  body = At_HttpResponse();
  if (strstr(body, "unauthorized") != NULL)
  {
    return HAL_ERROR;
  }
  return HAL_OK;
}
