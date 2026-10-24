/* USART1 で AT を送り、エコーを読みながら応答を見る。 */

#include "at_cmd.h"
#include "app_clock.h"
#include "wifi_secret.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

#define AT_RSP_TIMEOUT_MS  1000U
#define AT_JOIN_TIMEOUT_MS 20000U
#define AT_HTTP_TIMEOUT_MS 20000U
#define AT_BOOT_PAYLOAD_MAX   1024U
#define AT_BOOT_IDLE_MS       800U
#define AT_CMD_MAX            240U

/* 1 回の応答。boot GET の分割 +HTTPCLIENT を収める。 */
static uint8_t at_rx[2048];
/* +HTTPCLIENT / +HTTPCGET 各行の data 部を連結する。 */
static uint8_t boot_payload[AT_BOOT_PAYLOAD_MAX];
static uint16_t boot_payload_len;
/* CWJAP のコマンド。パスワードを含むのでログへ出さない。 */
static uint8_t at_tx[AT_CMD_MAX + 1U];
/* boot GET 失敗時の at_rx 分類。 */
static uint8_t boot_rx_diag;

/* 受信に OK があり、mark があればそれも含む。 */
static uint8_t at_rsp_ok(const char *mark)
{
  if (strstr((char *)at_rx, "OK") == NULL)
  {
    return 0U;
  }
  if (mark != NULL && strstr((char *)at_rx, mark) == NULL)
  {
    return 0U;
  }
  return 1U;
}

/* 届いているバイトを at_rx に足す。 */
static void at_take_rx(uint16_t *got)
{
  while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) != RESET && *got < (sizeof(at_rx) - 1U))
  {
    at_rx[*got] = (uint8_t)(huart1.Instance->DR & 0xFFU);
    (*got)++;
    at_rx[*got] = 0U;
  }
}

/* cmd に CR LF を付けて送り、OK を待つ。mark が NULL でなければその文字列も要る。 */
static HAL_StatusTypeDef at_command(const char *cmd, const char *mark, uint32_t timeout_ms)
{
  uint16_t got;
  uint32_t t0;
  uint32_t len;
  uint32_t index;
  uint8_t byte;

  if (cmd == NULL)
  {
    return HAL_ERROR;
  }
  len = 0U;
  while (cmd[len] != '\0')
  {
    len++;
    if (len > AT_CMD_MAX)
    {
      return HAL_ERROR;
    }
  }

  memset(at_rx, 0, sizeof(at_rx));
  got = 0U;
  __HAL_UART_CLEAR_OREFLAG(&huart1);
  t0 = HAL_GetTick();
  for (index = 0U; index < len + 2U; index++)
  {
    if (index < len)
    {
      byte = (uint8_t)cmd[index];
    }
    else if (index == len)
    {
      byte = (uint8_t)'\r';
    }
    else
    {
      byte = (uint8_t)'\n';
    }
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE) == RESET)
    {
      if ((HAL_GetTick() - t0) > timeout_ms)
      {
        return HAL_ERROR;
      }
    }
    huart1.Instance->DR = byte;
    at_take_rx(&got);
  }

  while ((HAL_GetTick() - t0) < timeout_ms && got < (sizeof(at_rx) - 1U))
  {
    at_take_rx(&got);
    if (strstr((char *)at_rx, "ERROR") != NULL)
    {
      return HAL_ERROR;
    }
    if (got > 0U && at_rsp_ok(mark) != 0U)
    {
      return HAL_OK;
    }
  }
  if (at_rsp_ok(mark) != 0U)
  {
    return HAL_OK;
  }
  return HAL_ERROR;
}

/* 1 文字足す。溢れたら 0。 */
static uint8_t at_put(uint16_t *n, char ch)
{
  if ((uint32_t)(*n) + 1U >= sizeof(at_tx))
  {
    return 0U;
  }
  at_tx[*n] = (uint8_t)ch;
  (*n)++;
  at_tx[*n] = 0U;
  return 1U;
}

/* そのまま足す。コマンドの枠に使う。 */
static uint8_t at_put_raw(uint16_t *n, const char *text)
{
  uint32_t index;

  if (text == NULL)
  {
    return 0U;
  }
  for (index = 0U; text[index] != '\0'; index++)
  {
    if (at_put(n, text[index]) == 0U)
    {
      return 0U;
    }
  }
  return 1U;
}

/* ESP-AT の文字列として足す。 , " \ をエスケープする。 */
static uint8_t at_put_escaped(uint16_t *n, const char *text)
{
  uint32_t index;

  if (text == NULL)
  {
    return 0U;
  }
  for (index = 0U; text[index] != '\0'; index++)
  {
    if (text[index] == ',' || text[index] == '"' || text[index] == '\\')
    {
      if (at_put(n, '\\') == 0U)
      {
        return 0U;
      }
    }
    if (at_put(n, text[index]) == 0U)
    {
      return 0U;
    }
  }
  return 1U;
}

/* IP を持っていれば参加しない。未接続なら CWJAP を 1 回送る。 */
static HAL_StatusTypeDef at_wifi(void)
{
  uint16_t n;

  if (at_command("AT+CWSTATE?", NULL, AT_RSP_TIMEOUT_MS) == HAL_OK)
  {
    if (strstr((char *)at_rx, "+CWSTATE:2,") != NULL)
    {
      return HAL_OK;
    }
    if (strstr((char *)at_rx, "+CWSTATE:3,") != NULL)
    {
      return HAL_ERROR;
    }
  }

  if (at_command("AT+CWMODE=1", NULL, AT_RSP_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (WIFI_SSID[0] == '\0')
  {
    return HAL_ERROR;
  }

  n = 0U;
  at_tx[0] = 0U;
  if (at_put_raw(&n, "AT+CWJAP=\"") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_escaped(&n, WIFI_SSID) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, "\",\"") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_escaped(&n, WIFI_PASS) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put(&n, '"') == 0U)
  {
    return HAL_ERROR;
  }
  return at_command((char *)at_tx, "WIFI GOT IP", AT_JOIN_TIMEOUT_MS);
}

/* バイト列を送る。got があれば受信も足す。 */
static HAL_StatusTypeDef at_send_bytes(const uint8_t *data, uint32_t len,
                                       uint32_t timeout_ms, uint16_t *got)
{
  uint32_t t0;
  uint32_t index;

  t0 = HAL_GetTick();
  for (index = 0U; index < len; index++)
  {
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE) == RESET)
    {
      if ((HAL_GetTick() - t0) > timeout_ms)
      {
        return HAL_ERROR;
      }
    }
    huart1.Instance->DR = data[index];
    if (got != NULL)
    {
      at_take_rx(got);
    }
  }
  return HAL_OK;
}

/* mark が出るか、ERROR になるまで待つ。 */
static HAL_StatusTypeDef at_wait_mark(uint16_t *got, const char *mark, uint32_t timeout_ms)
{
  uint32_t t0;

  t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < timeout_ms && *got < (sizeof(at_rx) - 1U))
  {
    at_take_rx(got);
    if (strstr((char *)at_rx, "SEND FAIL") != NULL)
    {
      return HAL_ERROR;
    }
    if (strstr((char *)at_rx, "ERROR") != NULL)
    {
      return HAL_ERROR;
    }
    if ((mark != NULL) && (strstr((char *)at_rx, mark) != NULL))
    {
      return HAL_OK;
    }
  }
  if ((mark != NULL) && (strstr((char *)at_rx, mark) != NULL))
  {
    return HAL_OK;
  }
  return HAL_ERROR;
}

/* AT コマンドとしての ERROR か。本文中の ERROR 文字列は無視する。 */
static uint8_t at_uart_is_error(void)
{
  const char *rx;

  rx = (char *)at_rx;
  if (strncmp(rx, "ERROR", 5) == 0)
  {
    return 1U;
  }
  if (strstr(rx, "\r\nERROR") != NULL)
  {
    return 1U;
  }
  if (strstr(rx, "\nERROR") != NULL)
  {
    return 1U;
  }
  return 0U;
}

/* URC 行の先頭か。AT+HTTPCLIENT: エコー内の部分一致を除く。 */
static uint8_t boot_urc_at_line_start(const char *line, const char *rx_start)
{
  if (line == rx_start)
  {
    return 1U;
  }
  if ((line > rx_start) && ((line[-1] == '\n') || (line[-1] == '\r')))
  {
    return 1U;
  }
  return 0U;
}

/* 応答 URC の +HTTPCLIENT: / +HTTPCGET:。行頭のみ。 */
static const char *boot_find_http_urc(const char *cursor)
{
  const char *hc;
  const char *hg;
  const char *line;
  const char *rx_start;

  if (cursor == NULL)
  {
    return NULL;
  }
  rx_start = (char *)at_rx;
  while (cursor[0] != '\0')
  {
    hc = strstr(cursor, "+HTTPCLIENT:");
    hg = strstr(cursor, "+HTTPCGET:");
    line = NULL;
    if ((hc != NULL) && (hg != NULL))
    {
      line = (hc < hg) ? hc : hg;
    }
    else if (hc != NULL)
    {
      line = hc;
    }
    else
    {
      line = hg;
    }
    if (line == NULL)
    {
      return NULL;
    }
    if (boot_urc_at_line_start(line, rx_start) != 0U)
    {
      return line;
    }
    cursor = line + 1U;
  }
  return NULL;
}

/* CR LF を飛ばす。 */
static const char *boot_skip_crlf(const char *cursor)
{
  while ((cursor != NULL) && ((cursor[0] == '\r') || (cursor[0] == '\n')))
  {
    cursor++;
  }
  return cursor;
}

/* cursor から最大 want バイトを payload へ足す。改行は跨ぐ。 */
static void boot_payload_append_from_cursor(const char **cursor, uint32_t want, const char *rx_end)
{
  uint32_t copied;

  if ((cursor == NULL) || (*cursor == NULL) || (rx_end == NULL))
  {
    return;
  }
  copied = 0U;
  while ((copied < want) && (*cursor < rx_end) && (**cursor != '\0'))
  {
    if (boot_payload_len >= (AT_BOOT_PAYLOAD_MAX - 1U))
    {
      break;
    }
    if ((*cursor[0] == '\r') || (*cursor[0] == '\n'))
    {
      *cursor = boot_skip_crlf(*cursor);
      continue;
    }
    boot_payload[boot_payload_len] = (uint8_t)(*cursor[0]);
    boot_payload_len++;
    boot_payload[boot_payload_len] = 0U;
    (*cursor)++;
    copied++;
  }
}

/* 連結結果または at_rx から JSON オブジェクトだけを切り出す。 */
static void boot_payload_finalize_json(const char *rx)
{
  const char *start;
  const char *end;
  const char *search;
  uint32_t len;

  search = (char *)boot_payload;
  if (boot_payload_len > 0U)
  {
    start = strchr(search, '{');
    if (start != NULL)
    {
      end = strrchr(start, '}');
      if ((end != NULL) && (end > start))
      {
        len = (uint32_t)(end - start + 1U);
        if (len >= AT_BOOT_PAYLOAD_MAX)
        {
          len = AT_BOOT_PAYLOAD_MAX - 1U;
        }
        if (start != search)
        {
          memmove(boot_payload, start, len);
        }
        boot_payload_len = (uint16_t)len;
        boot_payload[len] = 0U;
        return;
      }
    }
  }
  start = strchr(rx, '{');
  if (start == NULL)
  {
    return;
  }
  end = strrchr(start, '}');
  if ((end == NULL) || (end <= start))
  {
    return;
  }
  len = (uint32_t)(end - start + 1U);
  if (len >= AT_BOOT_PAYLOAD_MAX)
  {
    len = AT_BOOT_PAYLOAD_MAX - 1U;
  }
  memcpy(boot_payload, start, len);
  boot_payload_len = (uint16_t)len;
  boot_payload[len] = 0U;
}

/* +HTTPCLIENT / +HTTPCGET の size と data を連結する。行跨ぎとカンマ無しに対応。 */
static void boot_merge_http_payload(void)
{
  const char *rx;
  const char *rx_end;
  const char *cursor;
  const char *line;
  const char *colon;
  const char *mark;
  const char *chunk;
  uint32_t size;
  uint32_t digits;

  rx = (char *)at_rx;
  rx_end = rx;
  while ((rx_end[0] != '\0') && (rx_end < (char *)at_rx + sizeof(at_rx)))
  {
    rx_end++;
  }
  boot_payload_len = 0U;
  boot_payload[0] = 0U;
  cursor = rx;
  while ((cursor != NULL) && (cursor < rx_end) && (cursor[0] != '\0'))
  {
    line = boot_find_http_urc(cursor);
    if ((line == NULL) || (line >= rx_end))
    {
      break;
    }
    colon = strchr(line, ':');
    if (colon == NULL)
    {
      cursor = line + 11U;
      continue;
    }
    mark = colon + 1;
    size = 0U;
    digits = 0U;
    while ((mark < rx_end) && (mark[0] >= '0') && (mark[0] <= '9'))
    {
      size = (size * 10U) + (uint32_t)(mark[0] - '0');
      mark++;
      digits++;
    }
    if (digits == 0U)
    {
      cursor = line + 11U;
      continue;
    }
    if ((mark < rx_end) && (mark[0] == ','))
    {
      chunk = mark + 1;
      if (size == 0U)
      {
        while ((chunk < rx_end) && (chunk[0] != '\0') && (chunk[0] != '\r') && (chunk[0] != '\n'))
        {
          if (boot_payload_len >= (AT_BOOT_PAYLOAD_MAX - 1U))
          {
            break;
          }
          boot_payload[boot_payload_len] = (uint8_t)chunk[0];
          boot_payload_len++;
          chunk++;
        }
        boot_payload[boot_payload_len] = 0U;
        cursor = boot_skip_crlf(chunk);
      }
      else
      {
        boot_payload_append_from_cursor(&chunk, size, rx_end);
        cursor = chunk;
      }
    }
    else
    {
      chunk = boot_skip_crlf(mark);
      if (size == 0U)
      {
        cursor = line + 11U;
        continue;
      }
      boot_payload_append_from_cursor(&chunk, size, rx_end);
      cursor = chunk;
    }
  }
  boot_payload_finalize_json(rx);
}

/* boot 応答の探索は連結後を優先する。 */
static const char *boot_scan_view(void)
{
  boot_merge_http_payload();
  if (boot_payload_len > 0U)
  {
    return (char *)boot_payload;
  }
  return (char *)at_rx;
}

/* boot GET 失敗時に at_rx を分類する。 */
static uint8_t classify_boot_rx(void)
{
  const char *rx;
  const char *view;
  const char *client;
  const char *payload;
  uint16_t length;
  uint16_t index;
  uint8_t non_text;

  rx = (char *)at_rx;
  view = boot_scan_view();
  length = 0U;
  while ((rx[length] != '\0') && (length < (sizeof(at_rx) - 1U)))
  {
    length++;
  }
  if (length < 8U)
  {
    return APP_CLOCK_BOOT_DIAG_EMPTY;
  }
  client = boot_find_http_urc(rx);
  if (client != NULL)
  {
    if (strstr(view, "\"month\":") != NULL)
    {
      return APP_CLOCK_BOOT_DIAG_HTTP;
    }
    if (strstr(view, "{") != NULL || strstr(view, "\"ops\"") != NULL)
    {
      return APP_CLOCK_BOOT_DIAG_NOTIM;
    }
    payload = strchr(client, ',');
    if (payload != NULL)
    {
      payload++;
      non_text = 0U;
      for (index = 0U; (payload[index] != '\0') && (payload[index] != '\r') && (index < 48U); index++)
      {
        if ((payload[index] == '{') || (payload[index] == '"'))
        {
          non_text = 0U;
          break;
        }
        if ((uint8_t)payload[index] < 0x20U || (uint8_t)payload[index] > 0x7EU)
        {
          non_text = 1U;
        }
      }
      if (non_text != 0U)
      {
        return APP_CLOCK_BOOT_DIAG_GZIP;
      }
    }
    return APP_CLOCK_BOOT_DIAG_HTTPCL;
  }
  if (strstr(rx, "+HTTPERR:") != NULL)
  {
    return APP_CLOCK_BOOT_DIAG_HTTP;
  }
  if ((strstr(rx, "+HTTPCLIENT=") != NULL) || (strstr(rx, "AT+HTTPCLIENT") != NULL))
  {
    return APP_CLOCK_BOOT_DIAG_HTTPCL;
  }
  if ((strstr(rx, "OK") != NULL) && (length < 40U))
  {
    return APP_CLOCK_BOOT_DIAG_EMPTY;
  }
  return APP_CLOCK_BOOT_DIAG_HTTP;
}

uint8_t At_HttpBootRxDiag(void)
{
  return boot_rx_diag;
}

/* view と at_rx のどちらかに JSON キーがあるか。 */
static uint8_t boot_key_seen(const char *key)
{
  const char *view;

  view = boot_scan_view();
  if (strstr(view, key) != NULL)
  {
    return 1U;
  }
  if (strstr((char *)at_rx, key) != NULL)
  {
    return 1U;
  }
  return 0U;
}

/* boot GET 受信内容を AppClock へ渡す。 */
static void boot_publish_snap(uint16_t got)
{
  uint8_t keys;
  uint8_t urc;

  boot_merge_http_payload();
  keys = 0U;
  if (boot_key_seen("\"year\"") != 0U)
  {
    keys |= APP_CLOCK_BOOT_SNAP_YEAR;
  }
  if (boot_key_seen("\"month\"") != 0U)
  {
    keys |= APP_CLOCK_BOOT_SNAP_MONTH;
  }
  if (boot_key_seen("\"day\"") != 0U)
  {
    keys |= APP_CLOCK_BOOT_SNAP_DAY;
  }
  if (boot_key_seen("\"hour\"") != 0U)
  {
    keys |= APP_CLOCK_BOOT_SNAP_HOUR;
  }
  if (boot_key_seen("\"minute\"") != 0U)
  {
    keys |= APP_CLOCK_BOOT_SNAP_MINUTE;
  }
  {
    const char *urc_line;

    urc = APP_CLOCK_BOOT_URC_NONE;
    urc_line = boot_find_http_urc((char *)at_rx);
    if (urc_line != NULL)
    {
      if (strncmp(urc_line, "+HTTPCLIENT:", 12) == 0)
      {
        urc = APP_CLOCK_BOOT_URC_CLIENT;
      }
      else
      {
        urc = APP_CLOCK_BOOT_URC_GET;
      }
    }
    else if (strstr((char *)at_rx, "+HTTPCLIENT=") != NULL)
    {
      urc = APP_CLOCK_BOOT_URC_ECHO;
    }
    else if (strstr((char *)at_rx, "AT+HTTPCLIENT") != NULL)
    {
      urc = APP_CLOCK_BOOT_URC_ECHO;
    }
  }
  AppClock_SetBootSnap(got, boot_payload_len, keys, urc);
}

/* 数値暦キーが揃っているか。 */
static uint8_t boot_has_numeric_clock(const char *text)
{
  if (text == NULL)
  {
    return 0U;
  }
  if (strstr(text, "\"year\":") == NULL || strstr(text, "\"month\":") == NULL ||
      strstr(text, "\"day\":") == NULL || strstr(text, "\"hour\":") == NULL ||
      strstr(text, "\"minute\":") == NULL)
  {
    return 0U;
  }
  return 1U;
}

/* 起動時 ops 応答に同期に使える時刻があるか。 */
static uint8_t at_rx_has_boot_json(void)
{
  const char *view;

  view = boot_scan_view();
  if (boot_has_numeric_clock(view) != 0U)
  {
    return 1U;
  }
  if (boot_has_numeric_clock((char *)at_rx) != 0U)
  {
    return 1U;
  }
  return 0U;
}

/* boot GET の本文が at_rx に載るまで待つ。OK 後も分割 URC を読む。 */
static HAL_StatusTypeDef at_wait_boot_json(uint16_t *got, uint32_t timeout_ms)
{
  uint32_t t0;
  uint32_t last_activity;
  uint8_t seen_ok;
  uint8_t seen_http;
  uint16_t prev_got;

  t0 = HAL_GetTick();
  last_activity = t0;
  seen_ok = 0U;
  seen_http = 0U;
  while ((HAL_GetTick() - t0) < timeout_ms && *got < (sizeof(at_rx) - 1U))
  {
    prev_got = *got;
    at_take_rx(got);
    if (*got > prev_got)
    {
      last_activity = HAL_GetTick();
    }
    if (strstr((char *)at_rx, "SEND FAIL") != NULL)
    {
      return HAL_ERROR;
    }
    if (at_uart_is_error() != 0U)
    {
      return HAL_ERROR;
    }
    if (strstr((char *)at_rx, "OK") != NULL)
    {
      seen_ok = 1U;
    }
    if (boot_find_http_urc((char *)at_rx) != NULL)
    {
      seen_http = 1U;
    }
    if (at_rx_has_boot_json() != 0U)
    {
      return HAL_OK;
    }
    if ((seen_ok != 0U) && (seen_http != 0U) &&
        ((HAL_GetTick() - last_activity) >= AT_BOOT_IDLE_MS))
    {
      break;
    }
  }
  if (at_rx_has_boot_json() != 0U)
  {
    return HAL_OK;
  }
  return HAL_ERROR;
}

/* コマンドを送り、mark を待つ。 */
static HAL_StatusTypeDef at_command_mark(const char *cmd, const char *mark, uint32_t timeout_ms)
{
  uint16_t got;
  uint32_t len;
  uint8_t line[AT_CMD_MAX + 3U];

  if (cmd == NULL)
  {
    return HAL_ERROR;
  }
  len = 0U;
  while (cmd[len] != '\0')
  {
    len++;
    if (len > AT_CMD_MAX)
    {
      return HAL_ERROR;
    }
  }
  memcpy(line, cmd, len);
  line[len] = (uint8_t)'\r';
  line[len + 1U] = (uint8_t)'\n';

  memset(at_rx, 0, sizeof(at_rx));
  got = 0U;
  __HAL_UART_CLEAR_OREFLAG(&huart1);
  if (at_send_bytes(line, len + 2U, timeout_ms, &got) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return at_wait_mark(&got, mark, timeout_ms);
}

/* 10 進数を足す。 */
static uint8_t at_put_u16(uint16_t *n, uint16_t value)
{
  char tmp[5];
  uint8_t count;

  count = 0U;
  if (value == 0U)
  {
    return at_put(n, '0');
  }
  while ((value > 0U) && (count < 5U))
  {
    tmp[count] = (char)('0' + (value % 10U));
    count++;
    value = (uint16_t)(value / 10U);
  }
  while (count > 0U)
  {
    count--;
    if (at_put(n, tmp[count]) == 0U)
    {
      return 0U;
    }
  }
  return 1U;
}

/* HTTP ヘッダ。トークンはコマンド行に載せない。 */
static uint8_t at_http_head[192];

/* at_http_head に文字列を足す。 */
static uint8_t at_head_text(uint16_t *n, const char *text)
{
  uint32_t index;

  if (text == NULL)
  {
    return 0U;
  }
  for (index = 0U; text[index] != '\0'; index++)
  {
    if (*n >= (sizeof(at_http_head) - 1U))
    {
      return 0U;
    }
    at_http_head[*n] = (uint8_t)text[index];
    (*n)++;
  }
  at_http_head[*n] = 0U;
  return 1U;
}

/* HTTP 要求ヘッダをクリアしてから載せる。 */
static HAL_StatusTypeDef at_http_auth(const char *token, uint8_t boot_ops)
{
  uint16_t n;
  uint16_t got;
  uint16_t len;

  if ((token == NULL) || (token[0] == '\0'))
  {
    return HAL_ERROR;
  }
  if (at_command_mark("AT+HTTPCHEAD=0", "OK", AT_RSP_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }
  n = 0U;
  at_http_head[0] = 0U;
  if (at_put_raw(&n, "Authorization: Bearer ") == 0U)
  {
    return HAL_ERROR;
  }
  /* at_put_raw は at_tx に書く。ヘッダ用に移す。 */
  memcpy(at_http_head, at_tx, n);
  len = 0U;
  while (token[len] != '\0')
  {
    if ((n >= (sizeof(at_http_head) - 40U)) || (token[len] == '"') || (token[len] == '\r'))
    {
      return HAL_ERROR;
    }
    at_http_head[n] = (uint8_t)token[len];
    n++;
    len++;
  }
  at_http_head[n] = (uint8_t)'\r';
  n++;
  at_http_head[n] = (uint8_t)'\n';
  n++;
  if (boot_ops != 0U)
  {
    if (at_head_text(&n, "X-Device-Boot: 1\r\n") == 0U)
    {
      return HAL_ERROR;
    }
  }
  if (at_head_text(&n, "Content-Type: application/json\r\n") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_head_text(&n, "Accept-Encoding: identity\r\n") == 0U)
  {
    return HAL_ERROR;
  }
  at_http_head[n] = 0U;

  at_tx[0] = 0U;
  len = 0U;
  if (at_put_raw(&len, "AT+HTTPCHEAD=") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_u16(&len, n) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_command_mark((char *)at_tx, ">", AT_RSP_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }

  memset(at_rx, 0, sizeof(at_rx));
  got = 0U;
  if (at_send_bytes(at_http_head, n, AT_HTTP_TIMEOUT_MS, &got) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return at_wait_mark(&got, "OK", AT_HTTP_TIMEOUT_MS);
}

HAL_StatusTypeDef At_HttpAuth(const char *token)
{
  return at_http_auth(token, 0U);
}

HAL_StatusTypeDef At_HttpAuthBoot(const char *token)
{
  return at_http_auth(token, 1U);
}

const char *At_HttpResponse(void)
{
  return (const char *)at_rx;
}

const char *At_HttpBootBody(void)
{
  const char *json;

  boot_merge_http_payload();
  if (boot_payload_len == 0U)
  {
    return (const char *)at_rx;
  }
  json = strchr((char *)boot_payload, '{');
  if (json != NULL)
  {
    return json;
  }
  return (char *)boot_payload;
}

HAL_StatusTypeDef At_HttpGet(const char *url)
{
  uint16_t n;

  if ((url == NULL) || (url[0] == '\0'))
  {
    return HAL_ERROR;
  }
  n = 0U;
  at_tx[0] = 0U;
  if (at_put_raw(&n, "AT+HTTPCLIENT=2,1,\"") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, url) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, "\",,,2") == 0U)
  {
    return HAL_ERROR;
  }
  return at_command_mark((char *)at_tx, "OK", AT_HTTP_TIMEOUT_MS);
}

/* HTTPS の SNI を host に合わせる。 */
static HAL_StatusTypeDef at_http_sni(const char *host)
{
  uint16_t n;

  if ((host == NULL) || (host[0] == '\0'))
  {
    return HAL_ERROR;
  }
  n = 0U;
  at_tx[0] = 0U;
  if (at_put_raw(&n, "AT+HTTPCSNI=\"") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_escaped(&n, host) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put(&n, '"') == 0U)
  {
    return HAL_ERROR;
  }
  return at_command_mark((char *)at_tx, "OK", AT_RSP_TIMEOUT_MS);
}

/* boot GET 前に at_rx を空にする。 */
static void at_boot_rx_reset(void)
{
  memset(at_rx, 0, sizeof(at_rx));
  boot_payload_len = 0U;
  boot_payload[0] = 0U;
  __HAL_UART_CLEAR_OREFLAG(&huart1);
}

/* 組み立て済み AT 行を送り boot JSON を待つ。 */
static HAL_StatusTypeDef at_boot_exec_cmd(uint16_t *got)
{
  uint32_t len;
  uint8_t line[AT_CMD_MAX + 3U];

  len = 0U;
  while (((char *)at_tx)[len] != '\0')
  {
    len++;
    if (len > AT_CMD_MAX)
    {
      return HAL_ERROR;
    }
  }
  memcpy(line, at_tx, len);
  line[len] = (uint8_t)'\r';
  line[len + 1U] = (uint8_t)'\n';
  if (at_send_bytes(line, len + 2U, AT_HTTP_TIMEOUT_MS, got) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return at_wait_boot_json(got, AT_HTTP_TIMEOUT_MS);
}

/* HTTPCGET で boot GET。HTTPCHEAD が効く。 */
static HAL_StatusTypeDef at_boot_try_httpcget(const char *host, const char *path, uint16_t *got)
{
  uint16_t n;

  n = 0U;
  at_tx[0] = 0U;
  if (at_put_raw(&n, "AT+HTTPCGET=\"https://") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, host) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, path) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, "\",2048,2048,20000") == 0U)
  {
    return HAL_ERROR;
  }
  return at_boot_exec_cmd(got);
}

/* host/path 指定の HTTPCLIENT。URL だけより短い。 */
static HAL_StatusTypeDef at_boot_try_httpclient_host(const char *host, const char *path, uint16_t *got)
{
  uint16_t n;

  n = 0U;
  at_tx[0] = 0U;
  if (at_put_raw(&n, "AT+HTTPCLIENT=2,0,\"\",\"") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_escaped(&n, host) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, "\",\"") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_escaped(&n, path) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, "\",2") == 0U)
  {
    return HAL_ERROR;
  }
  return at_boot_exec_cmd(got);
}

/* 従来どおり URL 一体型の HTTPCLIENT。 */
static HAL_StatusTypeDef at_boot_try_httpclient_url(const char *host, const char *path, uint16_t *got)
{
  uint16_t n;

  n = 0U;
  at_tx[0] = 0U;
  if (at_put_raw(&n, "AT+HTTPCLIENT=2,0,\"https://") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, host) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, path) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, "\",,,2") == 0U)
  {
    return HAL_ERROR;
  }
  return at_boot_exec_cmd(got);
}

HAL_StatusTypeDef At_HttpGetBoot(const char *host, const char *path)
{
  uint16_t got;
  static uint8_t boot_echo_off;

  if ((host == NULL) || (host[0] == '\0') || (path == NULL) || (path[0] != '/'))
  {
    return HAL_ERROR;
  }
  if (boot_echo_off == 0U)
  {
    (void)at_command("ATE0", NULL, AT_RSP_TIMEOUT_MS);
    boot_echo_off = 1U;
  }
  (void)at_http_sni(host);

  at_boot_rx_reset();
  got = 0U;
  boot_rx_diag = APP_CLOCK_BOOT_DIAG_HTTP;
  if (at_boot_try_httpcget(host, path, &got) == HAL_OK)
  {
    boot_publish_snap(got);
    boot_rx_diag = APP_CLOCK_BOOT_DIAG_NONE;
    return HAL_OK;
  }

  at_boot_rx_reset();
  got = 0U;
  if (at_boot_try_httpclient_host(host, path, &got) == HAL_OK)
  {
    boot_publish_snap(got);
    boot_rx_diag = APP_CLOCK_BOOT_DIAG_NONE;
    return HAL_OK;
  }

  at_boot_rx_reset();
  got = 0U;
  if (at_boot_try_httpclient_url(host, path, &got) == HAL_OK)
  {
    boot_publish_snap(got);
    boot_rx_diag = APP_CLOCK_BOOT_DIAG_NONE;
    return HAL_OK;
  }

  boot_rx_diag = classify_boot_rx();
  boot_publish_snap(got);
  return HAL_ERROR;
}

HAL_StatusTypeDef At_HttpPost(const char *url, const char *body)
{
  uint16_t n;
  uint16_t got;
  uint16_t body_len;

  if ((url == NULL) || (body == NULL))
  {
    return HAL_ERROR;
  }
  body_len = 0U;
  while (body[body_len] != '\0')
  {
    body_len++;
    if (body_len > 180U)
    {
      return HAL_ERROR;
    }
  }

  n = 0U;
  at_tx[0] = 0U;
  if (at_put_raw(&n, "AT+HTTPCPOST=\"") == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_raw(&n, url) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put(&n, '"') == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put(&n, ',') == 0U)
  {
    return HAL_ERROR;
  }
  if (at_put_u16(&n, body_len) == 0U)
  {
    return HAL_ERROR;
  }
  if (at_command_mark((char *)at_tx, ">", AT_HTTP_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }

  memset(at_rx, 0, sizeof(at_rx));
  got = 0U;
  if (at_send_bytes((const uint8_t *)body, body_len, AT_HTTP_TIMEOUT_MS, &got) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return at_wait_mark(&got, "OK", AT_HTTP_TIMEOUT_MS);
}

HAL_StatusTypeDef At_Probe(void)
{
  if (at_command("AT", NULL, AT_RSP_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (at_command("AT+GMR", "MINI-1", AT_RSP_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return at_wifi();
}

HAL_StatusTypeDef At_Disconnect(void)
{
  if (at_command("AT+CWRECONNCFG=0,0", NULL, AT_RSP_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return at_command("AT+CWQAP", NULL, AT_RSP_TIMEOUT_MS);
}
