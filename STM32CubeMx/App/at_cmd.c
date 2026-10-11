/* USART1 で AT を送り、エコーを読みながら応答を見る。 */

#include "at_cmd.h"
#include "wifi_secret.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

#define AT_RSP_TIMEOUT_MS  1000U
#define AT_JOIN_TIMEOUT_MS 20000U
#define AT_CMD_MAX         240U

/* 1 回の応答。測定周期の外からは使わない。 */
static uint8_t at_rx[256];
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
