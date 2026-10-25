/* USART1 で AT を送り、エコーを読みながら応答を見る。 */

#include "at_cmd.h"
#include "wifi_secret.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

#define AT_RSP_TIMEOUT_MS  1000U
#define AT_JOIN_TIMEOUT_MS 20000U
#define AT_HTTP_TIMEOUT_MS 20000U
#define AT_BOOT_PAYLOAD_MAX   1024U
#define AT_BOOT_POST_OK_MS    1200U
#define AT_CMD_MAX            240U

/* 1 回の応答。boot GET の分割 +HTTPCLIENT を収める。 */
static uint8_t at_rx[2048];
/* +HTTPCLIENT / +HTTPCGET 各行の data 部を連結する。 */
static uint8_t boot_payload[AT_BOOT_PAYLOAD_MAX];
static uint16_t boot_payload_len;
/* CWJAP のコマンド。パスワードを含むのでログへ出さない。 */
static uint8_t at_tx[AT_CMD_MAX + 1U];

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

/* URC 行の +HTTPCLIENT: / +HTTPCGET: 以降のオフセット。 */
static uint8_t boot_urc_prefix_len(const char *line)
{
  if (strncmp(line, "+HTTPCLIENT:", 12) == 0)
  {
    return 12U;
  }
  if (strncmp(line, "+HTTPCGET:", 10) == 0)
  {
    return 10U;
  }
  return 1U;
}

/* 連結 payload から JSON オブジェクトまたは配列だけを切り出す。 */
static void boot_payload_finalize_json(void)
{
  const char *start;
  const char *end;
  const char *search;
  const char *array;
  const char *object;
  uint32_t len;

  search = (char *)boot_payload;
  if (boot_payload_len == 0U)
  {
    return;
  }
  array = strchr(search, '[');
  object = strchr(search, '{');
  start = NULL;
  if ((array != NULL) && ((object == NULL) || (array < object)))
  {
    start = array;
    end = strchr(start, ']');
  }
  else if (object != NULL)
  {
    start = object;
    end = strrchr(start, '}');
  }
  if ((start != NULL) && (end != NULL) && (end > start))
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
  }
}

/* 行末まで。数字とカンマだけ payload へ足す。 */
static void boot_payload_append_csv_range(const char **cursor, const char *line_end)
{
  char ch;

  if ((cursor == NULL) || (*cursor == NULL) || (line_end == NULL))
  {
    return;
  }
  while ((*cursor < line_end) && (**cursor != '\0'))
  {
    ch = **cursor;
    if (ch == '\r')
    {
      (*cursor)++;
      continue;
    }
    if (ch == '\n')
    {
      break;
    }
    if (((ch >= '0') && (ch <= '9')) || (ch == ','))
    {
      if (boot_payload_len >= (AT_BOOT_PAYLOAD_MAX - 1U))
      {
        break;
      }
      boot_payload[boot_payload_len] = (uint8_t)ch;
      boot_payload_len++;
      boot_payload[boot_payload_len] = 0U;
    }
    (*cursor)++;
  }
}

/* 行が数字とカンマだけか。 */
static uint8_t boot_line_is_csv_only(const char *start, const char *end)
{
  const char *mark;

  if ((start == NULL) || (end == NULL) || (start >= end))
  {
    return 0U;
  }
  if ((start[0] < '0') || (start[0] > '9'))
  {
    return 0U;
  }
  mark = start;
  while (mark < end)
  {
    if ((mark[0] == '\r') || (mark[0] == '\n'))
    {
      break;
    }
    if (((mark[0] >= '0') && (mark[0] <= '9')) || (mark[0] == ','))
    {
      mark++;
      continue;
    }
    return 0U;
  }
  return 1U;
}

/* 行末まで JSON 断片を payload へ足す。 */
static void boot_payload_append_json_range(const char **cursor, const char *line_end)
{
  char ch;

  if ((cursor == NULL) || (*cursor == NULL) || (line_end == NULL))
  {
    return;
  }
  while ((*cursor < line_end) && (**cursor != '\0'))
  {
    ch = **cursor;
    if (ch == '\r')
    {
      (*cursor)++;
      continue;
    }
    if (ch == '\n')
    {
      break;
    }
    if (boot_payload_len >= (AT_BOOT_PAYLOAD_MAX - 1U))
    {
      break;
    }
    boot_payload[boot_payload_len] = (uint8_t)ch;
    boot_payload_len++;
    boot_payload[boot_payload_len] = 0U;
    (*cursor)++;
  }
}

/* notifyMode が読めるか。前方宣言。 */
static uint8_t boot_merged_notify_ready(const char *text);

/* URC 行以外の JSON 続き行を足す。 */
static void boot_merge_bare_json_lines(const char *rx, const char *rx_end)
{
  const char *cursor;
  const char *line_end;
  const char *chunk;

  if (boot_payload_len == 0U)
  {
    return;
  }
  if (boot_merged_notify_ready((char *)boot_payload) != 0U)
  {
    return;
  }
  cursor = rx;
  while ((cursor < rx_end) && (cursor[0] != '\0'))
  {
    line_end = cursor;
    while ((line_end < rx_end) && (line_end[0] != '\0') && (line_end[0] != '\n'))
    {
      line_end++;
    }
    if (boot_urc_at_line_start(cursor, rx) != 0U)
    {
      if ((strncmp(cursor, "+HTTPCLIENT:", 12) == 0) || (strncmp(cursor, "+HTTPCGET:", 10) == 0))
      {
        cursor = line_end;
        if ((cursor < rx_end) && (cursor[0] == '\n'))
        {
          cursor++;
        }
        continue;
      }
    }
    if ((cursor[0] == 'O') && (strncmp(cursor, "OK", 2) == 0))
    {
      cursor = line_end;
      if ((cursor < rx_end) && (cursor[0] == '\n'))
      {
        cursor++;
      }
      continue;
    }
    if ((cursor[0] == 'E') && (strncmp(cursor, "ERROR", 5) == 0))
    {
      cursor = line_end;
      if ((cursor < rx_end) && (cursor[0] == '\n'))
      {
        cursor++;
      }
      continue;
    }
    if (strncmp(cursor, "AT+", 3) == 0)
    {
      cursor = line_end;
      if ((cursor < rx_end) && (cursor[0] == '\n'))
      {
        cursor++;
      }
      continue;
    }
    if (strchr(cursor, '{') != NULL)
    {
      chunk = strchr(cursor, '{');
      if (chunk < line_end)
      {
        boot_payload_append_json_range(&chunk, line_end);
      }
    }
    else if (strchr(cursor, '"') != NULL)
    {
      chunk = cursor;
      boot_payload_append_json_range(&chunk, line_end);
    }
    else if (boot_line_is_csv_only(cursor, line_end) != 0U)
    {
      chunk = cursor;
      boot_payload_append_csv_range(&chunk, line_end);
    }
    cursor = line_end;
    if ((cursor < rx_end) && (cursor[0] == '\n'))
    {
      cursor++;
    }
  }
}

/* 1 行の +HTTPCLIENT / +HTTPCGET から data 部を行末まで payload へ足す。 */
static void boot_payload_append_urc_line(const char *line, const char *rx_end)
{
  const char *colon;
  const char *mark;
  const char *chunk;
  const char *line_end;
  uint8_t had_digit;

  if ((line == NULL) || (rx_end == NULL))
  {
    return;
  }
  line_end = line;
  while ((line_end < rx_end) && (line_end[0] != '\0') && (line_end[0] != '\n'))
  {
    line_end++;
  }
  colon = strchr(line, ':');
  if (colon == NULL)
  {
    return;
  }
  mark = colon + 1;
  had_digit = 0U;
  while ((mark < line_end) && (mark[0] >= '0') && (mark[0] <= '9'))
  {
    mark++;
    had_digit = 1U;
  }
  if (had_digit == 0U)
  {
    return;
  }
  chunk = strchr(mark, '{');
  if ((chunk != NULL) && (chunk < line_end))
  {
    boot_payload_append_json_range(&chunk, line_end);
    return;
  }
  if ((mark < line_end) && (mark[0] == ','))
  {
    chunk = mark + 1;
    if ((chunk[0] < '0') || (chunk[0] > '9'))
    {
      return;
    }
    boot_payload_append_csv_range(&chunk, line_end);
  }
}

/* +HTTPCLIENT / +HTTPCGET 各行の JSON 部を連結する。 */
static void boot_merge_http_payload(void)
{
  const char *rx;
  const char *rx_end;
  const char *cursor;
  const char *line;

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
    boot_payload_append_urc_line(line, rx_end);
    cursor = line + boot_urc_prefix_len(line);
    while ((cursor < rx_end) && (cursor[0] != '\0') && (cursor[0] != '\n'))
    {
      cursor++;
    }
    if ((cursor < rx_end) && (cursor[0] == '\n'))
    {
      cursor++;
    }
  }
  boot_merge_bare_json_lines(rx, rx_end);
  boot_payload_finalize_json();
}

/* boot 応答の探索は URC 連結後のみ。 */
static const char *boot_scan_view(void)
{
  boot_merge_http_payload();
  return (char *)boot_payload;
}

/* 本文から notifyMode の JSON 値 0/1 を読む。 */
static uint8_t boot_parse_notify_json(const char *text, uint8_t *notify_mode)
{
  const char *mark;

  if ((text == NULL) || (notify_mode == NULL))
  {
    return 0U;
  }
  mark = strstr(text, "\"notifyMode\":");
  if (mark == NULL)
  {
    mark = strstr(text, "\"n\":");
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
  *notify_mode = (uint8_t)(*mark - '0');
  return 1U;
}

/* 連結 payload に notifyMode があるか。 */
static uint8_t boot_merged_notify_ready(const char *text)
{
  uint8_t notify_mode;

  if (text == NULL)
  {
    return 0U;
  }
  if (boot_payload_len == 0U)
  {
    return 0U;
  }
  return boot_parse_notify_json(text, &notify_mode);
}

/* 連結 payload が boot 取得に足りるか。at_rx 全体は見ない。 */
static uint8_t boot_merged_payload_ready(void)
{
  const char *view;

  view = boot_scan_view();
  return boot_merged_notify_ready(view);
}

/* boot GET の分割 URC を連結するまで読む。 */
static HAL_StatusTypeDef at_wait_boot_json(uint16_t *got, uint32_t timeout_ms)
{
  uint32_t t0;
  uint32_t last_activity;
  uint16_t prev_got;
  uint16_t prev_pl;
  uint8_t seen_ok;
  uint8_t seen_urc;

  t0 = HAL_GetTick();
  last_activity = t0;
  seen_ok = 0U;
  seen_urc = 0U;
  prev_pl = 0U;
  while ((HAL_GetTick() - t0) < timeout_ms && *got < (sizeof(at_rx) - 1U))
  {
    prev_got = *got;
    at_take_rx(got);
    if (*got > prev_got)
    {
      last_activity = HAL_GetTick();
    }
    boot_merge_http_payload();
    if (boot_payload_len > prev_pl)
    {
      prev_pl = boot_payload_len;
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
      seen_urc = 1U;
    }
    if ((boot_merged_payload_ready() != 0U) && (seen_ok != 0U) &&
        ((HAL_GetTick() - last_activity) >= AT_BOOT_POST_OK_MS))
    {
      return HAL_OK;
    }
  }
  if (boot_merged_payload_ready() != 0U)
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
  json = strchr((char *)boot_payload, '{');
  if (json != NULL)
  {
    return json;
  }
  json = strchr((char *)boot_payload, '[');
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
  if (at_boot_try_httpcget(host, path, &got) == HAL_OK)
  {
    boot_merge_http_payload();
    return HAL_OK;
  }

  at_boot_rx_reset();
  got = 0U;
  if (at_boot_try_httpclient_host(host, path, &got) == HAL_OK)
  {
    boot_merge_http_payload();
    return HAL_OK;
  }

  at_boot_rx_reset();
  got = 0U;
  if (at_boot_try_httpclient_url(host, path, &got) == HAL_OK)
  {
    boot_merge_http_payload();
    return HAL_OK;
  }

  boot_merge_http_payload();
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
