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

/* +HTTPCLIENT:<size>,<data> の data 部を boot_payload へ足す。 */
static void boot_merge_http_payload(void)
{
  const char *rx;
  const char *cursor;
  const char *line;
  const char *comma;
  const char *payload;
  const char *hc;
  const char *hg;
  uint16_t index;

  rx = (char *)at_rx;
  boot_payload_len = 0U;
  boot_payload[0] = 0U;
  cursor = rx;
  while ((cursor != NULL) && (cursor[0] != '\0'))
  {
    hc = strstr(cursor, "+HTTPCLIENT");
    hg = strstr(cursor, "+HTTPCGET");
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
      break;
    }
    comma = strchr(line, ',');
    if (comma == NULL)
    {
      cursor = line + 1U;
      continue;
    }
    payload = comma + 1;
    index = 0U;
    while ((payload[index] != '\0') && (payload[index] != '\r') && (payload[index] != '\n'))
    {
      if (boot_payload_len >= (AT_BOOT_PAYLOAD_MAX - 1U))
      {
        break;
      }
      boot_payload[boot_payload_len] = (uint8_t)payload[index];
      boot_payload_len++;
      index++;
    }
    boot_payload[boot_payload_len] = 0U;
    cursor = payload + index;
    if ((payload[index] == '\r') || (payload[index] == '\n'))
    {
      cursor++;
      if ((payload[index] == '\r') && (payload[index + 1U] == '\n'))
      {
        cursor++;
      }
    }
  }
  {
    const char *start;
    const char *end;
    uint32_t len;

    if (strchr((char *)boot_payload, '{') != NULL &&
        strstr((char *)boot_payload, "\"month\":") != NULL)
    {
      return;
    }
    start = strchr(rx, '{');
    if (start == NULL)
    {
      return;
    }
    end = strrchr(rx, '}');
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
  client = strstr(rx, "+HTTPCGET");
  if (client == NULL)
  {
    client = strstr(rx, "+HTTPCLIENT");
  }
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

/* boot GET の本文が at_rx に載るまで待つ。OK 後もタイムアウトまで読む。 */
static HAL_StatusTypeDef at_wait_boot_json(uint16_t *got, uint32_t timeout_ms)
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
    if (at_uart_is_error() != 0U)
    {
      return HAL_ERROR;
    }
    if (at_rx_has_boot_json() != 0U)
    {
      return HAL_OK;
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

HAL_StatusTypeDef At_HttpGetBoot(const char *host, const char *path)
{
  uint16_t n;
  uint16_t got;
  uint32_t len;
  uint8_t line[AT_CMD_MAX + 3U];

  if ((host == NULL) || (host[0] == '\0') || (path == NULL) || (path[0] != '/'))
  {
    return HAL_ERROR;
  }
  n = 0U;
  at_tx[0] = 0U;
  /* HTTPCGET は v3.3 で HTTPCHEAD が効かないことがある。POST と同じ HTTPCLIENT を使う。 */
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

  memset(at_rx, 0, sizeof(at_rx));
  boot_payload_len = 0U;
  boot_payload[0] = 0U;
  got = 0U;
  __HAL_UART_CLEAR_OREFLAG(&huart1);
  boot_rx_diag = APP_CLOCK_BOOT_DIAG_HTTP;
  if (at_send_bytes(line, len + 2U, AT_HTTP_TIMEOUT_MS, &got) != HAL_OK)
  {
    boot_rx_diag = APP_CLOCK_BOOT_DIAG_EMPTY;
    return HAL_ERROR;
  }
  if (at_wait_boot_json(&got, AT_HTTP_TIMEOUT_MS) != HAL_OK)
  {
    boot_rx_diag = classify_boot_rx();
    return HAL_ERROR;
  }
  boot_rx_diag = APP_CLOCK_BOOT_DIAG_NONE;
  return HAL_OK;
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
