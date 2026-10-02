#include "st7032.h"

/* ST7032i の 7bit アドレス 0x3E。HAL には左へ 1bit 寄せた値を渡す。 */
#define ST7032_I2C_ADDR_8BIT  (0x3EU << 1)
#define ST7032_CTRL_CMD       0x00U
#define ST7032_CTRL_DATA      0x40U
#define ST7032_I2C_TIMEOUT_MS 100U

/* Sitronix ST7032 Rev 1.4 の 8bit 初期化例。 */
#define ST7032_CMD_FUNCTION_SET     0x38U
#define ST7032_CMD_FUNCTION_SET_EXT 0x39U
#define ST7032_CMD_INTERNAL_OSC     0x14U
#define ST7032_CMD_CONTRAST_LOW     0x78U
#define ST7032_CMD_POWER_ICON       0x5EU
#define ST7032_CMD_FOLLOWER         0x6AU
#define ST7032_CMD_DISPLAY_ON       0x0CU
#define ST7032_CMD_CLEAR            0x01U
#define ST7032_CMD_ENTRY_MODE       0x06U

static HAL_StatusTypeDef st7032_command(I2C_HandleTypeDef *hi2c, uint8_t command)
{
  uint8_t payload[2];

  payload[0] = ST7032_CTRL_CMD;
  payload[1] = command;
  return HAL_I2C_Master_Transmit(hi2c, ST7032_I2C_ADDR_8BIT, payload, 2U, ST7032_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef ST7032_Init(I2C_HandleTypeDef *hi2c)
{
  HAL_StatusTypeDef status;

  /* 内部リセットの Busy は電源安定後、最大 40 ms。 */
  HAL_Delay(40);

  status = st7032_command(hi2c, ST7032_CMD_FUNCTION_SET);
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(1);

  status = st7032_command(hi2c, ST7032_CMD_FUNCTION_SET_EXT);
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(1);

  status = st7032_command(hi2c, ST7032_CMD_INTERNAL_OSC);
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(1);

  status = st7032_command(hi2c, ST7032_CMD_CONTRAST_LOW);
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(1);

  status = st7032_command(hi2c, ST7032_CMD_POWER_ICON);
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(1);

  status = st7032_command(hi2c, ST7032_CMD_FOLLOWER);
  if (status != HAL_OK)
  {
    return status;
  }
  /* フォロワ投入後、電源が安定するまで待つ。 */
  HAL_Delay(200);

  status = st7032_command(hi2c, ST7032_CMD_DISPLAY_ON);
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(1);

  status = st7032_command(hi2c, ST7032_CMD_CLEAR);
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(2);

  status = st7032_command(hi2c, ST7032_CMD_ENTRY_MODE);
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(1);

  return HAL_OK;
}

HAL_StatusTypeDef ST7032_WriteString(I2C_HandleTypeDef *hi2c, const char *text)
{
  uint8_t payload[2];
  HAL_StatusTypeDef status;

  if (text == NULL)
  {
    return HAL_ERROR;
  }

  payload[0] = ST7032_CTRL_DATA;
  while (*text != '\0')
  {
    payload[1] = (uint8_t)*text;
    status = HAL_I2C_Master_Transmit(hi2c, ST7032_I2C_ADDR_8BIT, payload, 2U, ST7032_I2C_TIMEOUT_MS);
    if (status != HAL_OK)
    {
      return status;
    }
    text++;
  }

  return HAL_OK;
}
