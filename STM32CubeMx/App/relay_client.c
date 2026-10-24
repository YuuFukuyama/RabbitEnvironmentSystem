/* 起動時に時刻と通知モードを取り、測定のたびに中継へ送る。 */

#include "relay_client.h"
#include "app_clock.h"
#include "at_cmd.h"
#include "env_judge.h"
#include "wifi_link.h"
#include "wifi_secret.h"
#include <string.h>

extern RTC_HandleTypeDef hrtc;

/* 起動後に時刻と通知モードを書けたら 1。 */
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

/* RTC カウンタを読む。 */
static uint32_t read_counter(void)
{
  uint16_t high1;
  uint16_t high2;
  uint16_t low;

  high1 = (uint16_t)(hrtc.Instance->CNTH & RTC_CNTH_RTC_CNT);
  low = (uint16_t)(hrtc.Instance->CNTL & RTC_CNTL_RTC_CNT);
  high2 = (uint16_t)(hrtc.Instance->CNTH & RTC_CNTH_RTC_CNT);
  if (high1 != high2)
  {
    low = (uint16_t)(hrtc.Instance->CNTL & RTC_CNTL_RTC_CNT);
    return (((uint32_t)high2) << 16) | low;
  }
  return (((uint32_t)high1) << 16) | low;
}

/* JSON のキー直後の整数を読む。 */
static uint8_t parse_json_u16(const char *body, const char *key, uint16_t *value)
{
  const char *mark;
  uint32_t number;
  uint8_t digits;

  mark = strstr(body, key);
  if (mark == NULL)
  {
    return 0U;
  }
  mark += strlen(key);
  while ((*mark == ' ') || (*mark == '\t'))
  {
    mark++;
  }
  if (*mark != ':')
  {
    return 0U;
  }
  mark++;
  while ((*mark == ' ') || (*mark == '\t'))
  {
    mark++;
  }
  number = 0U;
  digits = 0U;
  while ((*mark >= '0') && (*mark <= '9'))
  {
    number = (number * 10U) + (uint32_t)(*mark - '0');
    if (number > 65535U)
    {
      return 0U;
    }
    mark++;
    digits++;
  }
  if (digits == 0U)
  {
    return 0U;
  }
  *value = (uint16_t)number;
  return 1U;
}

/* 長いキーまたは短いキーで整数を読む。 */
static uint8_t parse_json_u16_alt(const char *body, const char *key_long, const char *key_short,
                                  uint16_t *value)
{
  if (parse_json_u16(body, key_long, value) != 0U)
  {
    return 1U;
  }
  if (key_short == NULL)
  {
    return 0U;
  }
  return parse_json_u16(body, key_short, value);
}

/* 暦要素を RTC へ書く。 */
static HAL_StatusTypeDef apply_rtc_fields(uint16_t year, uint16_t month, uint16_t day, uint16_t hour,
                                        uint16_t minute, uint16_t second)
{
  RTC_TimeTypeDef rtc_time;
  RTC_DateTypeDef rtc_date;

  if ((year < 2000U) || (year > 2099U) || (month < 1U) || (month > 12U) || (day < 1U) ||
      (day > 31U) || (hour > 23U) || (minute > 59U) || (second > 59U))
  {
    return HAL_ERROR;
  }
  if (hrtc.Instance != RTC)
  {
    return HAL_ERROR;
  }

  memset(&rtc_time, 0, sizeof(rtc_time));
  memset(&rtc_date, 0, sizeof(rtc_date));
  rtc_time.Hours = (uint8_t)hour;
  rtc_time.Minutes = (uint8_t)minute;
  rtc_time.Seconds = (uint8_t)second;
  rtc_date.Year = (uint8_t)(year - 2000U);
  rtc_date.Month = (uint8_t)month;
  rtc_date.Date = (uint8_t)day;
  rtc_date.WeekDay = RTC_WEEKDAY_MONDAY;
  if (HAL_RTC_SetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (HAL_RTC_SetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return HAL_OK;
}

/* CSV の先頭数字位置。[] 付きにも対応。 */
static const char *boot_csv_start(const char *body)
{
  const char *mark;

  if (body == NULL)
  {
    return NULL;
  }
  mark = body;
  if (mark[0] == '[')
  {
    mark++;
  }
  while ((mark[0] != '\0') && (mark[0] < '0' || mark[0] > '9'))
  {
    mark++;
  }
  if ((mark[0] < '0') || (mark[0] > '9'))
  {
    return NULL;
  }
  return mark;
}

/* 年,月,日,時,分[,notifyMode] の CSV を読む。 */
static uint8_t parse_boot_array(const char *body, uint16_t *year, uint16_t *month, uint16_t *day,
                                uint16_t *hour, uint16_t *minute, uint8_t *mode)
{
  const char *mark;
  uint8_t index;
  uint32_t number;
  uint8_t digits;
  uint16_t values[6];

  mark = boot_csv_start(body);
  if (mark == NULL)
  {
    return 0U;
  }
  for (index = 0U; index < 6U; index++)
  {
    while ((*mark == ' ') || (*mark == '\t'))
    {
      mark++;
    }
    if ((*mark < '0') || (*mark > '9'))
    {
      if (index == 5U)
      {
        values[5] = 0U;
        break;
      }
      return 0U;
    }
    number = 0U;
    digits = 0U;
    while ((*mark >= '0') && (*mark <= '9'))
    {
      number = (number * 10U) + (uint32_t)(*mark - '0');
      if (number > 65535U)
      {
        return 0U;
      }
      mark++;
      digits++;
    }
    if (digits == 0U)
    {
      return 0U;
    }
    values[index] = (uint16_t)number;
    while ((*mark == ' ') || (*mark == '\t'))
    {
      mark++;
    }
    if (index < 5U)
    {
      if (*mark != ',')
      {
        if (index == 4U)
        {
          values[5] = 0U;
          index = 5U;
          break;
        }
        return 0U;
      }
      mark++;
    }
    else if ((*mark != ']') && (*mark != '\0') && (*mark != '\r') && (*mark != '\n'))
    {
      return 0U;
    }
  }
  if (index < 4U)
  {
    return 0U;
  }
  if (index == 4U)
  {
    values[5] = 0U;
  }
  if (values[0] < 100U)
  {
    values[0] = (uint16_t)(values[0] + 2000U);
  }
  *year = values[0];
  *month = values[1];
  *day = values[2];
  *hour = values[3];
  *minute = values[4];
  if ((values[5] != 0U) && (values[5] != 1U))
  {
    return 0U;
  }
  *mode = (uint8_t)values[5];
  return 1U;
}

/* 応答の数値暦を RTC へ書く。 */
static HAL_StatusTypeDef apply_clock_from_body(const char *body)
{
  uint16_t year;
  uint16_t month;
  uint16_t day;
  uint16_t hour;
  uint16_t minute;
  uint16_t second;
  uint8_t mode;

  if (parse_boot_array(body, &year, &month, &day, &hour, &minute, &mode) != 0U)
  {
    second = 0U;
    return apply_rtc_fields(year, month, day, hour, minute, second);
  }
  if ((parse_json_u16_alt(body, "\"year\"", "\"y\":", &year) == 0U) ||
      (parse_json_u16_alt(body, "\"month\"", "\"M\":", &month) == 0U) ||
      (parse_json_u16_alt(body, "\"day\"", "\"d\":", &day) == 0U) ||
      (parse_json_u16_alt(body, "\"hour\"", "\"h\":", &hour) == 0U) ||
      (parse_json_u16_alt(body, "\"minute\"", "\"m\":", &minute) == 0U))
  {
    return HAL_ERROR;
  }
  second = 0U;
  (void)parse_json_u16_alt(body, "\"second\"", "\"s\":", &second);
  return apply_rtc_fields(year, month, day, hour, minute, second);
}

/* 応答から notifyMode を読む。 */
static uint8_t parse_mode(const char *body, uint8_t *mode)
{
  const char *mark;
  uint16_t year;
  uint16_t month;
  uint16_t day;
  uint16_t hour;
  uint16_t minute;

  if (parse_boot_array(body, &year, &month, &day, &hour, &minute, mode) != 0U)
  {
    return 1U;
  }
  mark = strstr(body, "\"notifyMode\":");
  if (mark == NULL)
  {
    mark = strstr(body, "\"n\":");
    if (mark == NULL)
    {
      return 0U;
    }
    mark += 4;
  }
  else
  {
    mark += 13;
  }
  while (*mark == ' ')
  {
    mark++;
  }
  if ((*mark != '0') && (*mark != '1'))
  {
    return 0U;
  }
  *mode = (uint8_t)(*mark - '0');
  return 1U;
}

uint8_t Relay_TakeBoot(uint32_t *synced_sec)
{
  const char *body;
  uint8_t mode;
  uint32_t old_sec;
  uint32_t new_sec;

  if ((boot_done != 0U) || (Relay_Enabled() == 0U) || (synced_sec == NULL))
  {
    return 0U;
  }
  if (At_HttpAuthBoot(DEVICE_TOKEN) != HAL_OK)
  {
    AppClock_SetBootDiag(APP_CLOCK_BOOT_DIAG_AUTH);
    return 0U;
  }
  if (At_HttpGetBoot(RELAY_HOST, "/v1/ops") != HAL_OK)
  {
    AppClock_SetBootDiag(At_HttpBootRxDiag());
    return 0U;
  }
  body = At_HttpBootBody();
  if (parse_mode(body, &mode) == 0U)
  {
    AppClock_SetBootDiag(APP_CLOCK_BOOT_DIAG_PARSE);
    return 0U;
  }
  if (hrtc.Instance == RTC)
  {
    old_sec = read_counter();
  }
  else
  {
    old_sec = 0U;
  }
  if (apply_clock_from_body(body) != HAL_OK)
  {
    if ((AppClock_HwReady() == 0U) || (hrtc.Instance != RTC))
    {
      AppClock_SetBootDiag(APP_CLOCK_BOOT_DIAG_NO_HW);
    }
    else
    {
      AppClock_SetBootDiag(APP_CLOCK_BOOT_DIAG_RTC);
    }
    return 0U;
  }
  AppClock_SetBootDiag(APP_CLOCK_BOOT_DIAG_NONE);
  AppClock_MarkSynced();
  new_sec = read_counter();
  EnvJudge_SetNotifyMode(mode);
  boot_done = 1U;
  *synced_sec = new_sec;
  if (new_sec == old_sec)
  {
    return 0U;
  }
  return 1U;
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
