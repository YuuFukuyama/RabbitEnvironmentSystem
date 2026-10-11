#include "st7032.h"

/* ST7032i の 7bit アドレス 0x3E。HAL には左へ 1bit 寄せた値を渡す。 */
#define ST7032_I2C_ADDR_8BIT  (0x3EU << 1)
#define ST7032_CTRL_CMD       0x00U
#define ST7032_CTRL_DATA      0x40U
#define ST7032_I2C_TIMEOUT_MS 100U

/* Sitronix ST7032 Rev 1.4 の 8bit 初期化。コントラストは 3.3V 向け（α=40、増幅率 2.0）。 */
#define ST7032_CMD_FUNCTION_SET     0x38U
#define ST7032_CMD_FUNCTION_SET_EXT 0x39U
#define ST7032_CMD_INTERNAL_OSC     0x14U
#define ST7032_CMD_CONTRAST_LOW     0x78U
#define ST7032_CMD_POWER_ICON       0x5EU
#define ST7032_CMD_FOLLOWER         0x6CU
#define ST7032_CMD_DISPLAY_ON       0x0CU
#define ST7032_CMD_CLEAR            0x01U
#define ST7032_CMD_ENTRY_MODE       0x06U
#define ST7032_CMD_DDRAM            0x80U
#define ST7032_CMD_ICON_ADDR        0x40U
#define ST7032_ICON_COUNT           16U
/* S56 はアドレス 0x0B の D4。S37 はアドレス 0x07 の D3。S1 はアドレス 0x00 の D4。 */
#define ST7032_ICON_S56_ADDR        0x0BU
#define ST7032_ICON_S56_BIT         0x10U
#define ST7032_ICON_S37_ADDR        0x07U
#define ST7032_ICON_S37_BIT         0x08U
#define ST7032_ICON_S1_ADDR         0x00U
#define ST7032_ICON_S1_BIT          0x10U

static HAL_StatusTypeDef st7032_command(I2C_HandleTypeDef *hi2c, uint8_t command)
{
  uint8_t payload[2];

  payload[0] = ST7032_CTRL_CMD;
  payload[1] = command;
  return HAL_I2C_Master_Transmit(hi2c, ST7032_I2C_ADDR_8BIT, payload, 2U, ST7032_I2C_TIMEOUT_MS);
}

/* RS=1 で 1 バイト書く。 */
static HAL_StatusTypeDef st7032_data(I2C_HandleTypeDef *hi2c, uint8_t data)
{
  uint8_t payload[2];

  payload[0] = ST7032_CTRL_DATA;
  payload[1] = data;
  return HAL_I2C_Master_Transmit(hi2c, ST7032_I2C_ADDR_8BIT, payload, 2U, ST7032_I2C_TIMEOUT_MS);
}

/* アイコン RAM を消してから、命令テーブルを通常へ戻す。 */
static HAL_StatusTypeDef st7032_clear_icons(I2C_HandleTypeDef *hi2c)
{
  uint8_t index;
  HAL_StatusTypeDef status;

  status = st7032_command(hi2c, ST7032_CMD_FUNCTION_SET_EXT);
  if (status != HAL_OK)
  {
    return status;
  }
  status = st7032_command(hi2c, ST7032_CMD_ICON_ADDR);
  if (status != HAL_OK)
  {
    return status;
  }
  for (index = 0U; index < ST7032_ICON_COUNT; index++)
  {
    status = st7032_data(hi2c, 0x00U);
    if (status != HAL_OK)
    {
      return status;
    }
  }
  return st7032_command(hi2c, ST7032_CMD_FUNCTION_SET);
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

  return st7032_clear_icons(hi2c);
}

/* ICON RAM の 1 バイトを書いて、命令テーブルを通常へ戻す。 */
static HAL_StatusTypeDef st7032_write_icon(I2C_HandleTypeDef *hi2c, uint8_t address, uint8_t data)
{
  HAL_StatusTypeDef status;

  status = st7032_command(hi2c, ST7032_CMD_FUNCTION_SET_EXT);
  if (status != HAL_OK)
  {
    return status;
  }
  status = st7032_command(hi2c, (uint8_t)(ST7032_CMD_ICON_ADDR | address));
  if (status != HAL_OK)
  {
    return status;
  }
  status = st7032_data(hi2c, data);
  if (status != HAL_OK)
  {
    return status;
  }
  return st7032_command(hi2c, ST7032_CMD_FUNCTION_SET);
}

/* on が 0 のときはそのアドレスを消す。 */
static HAL_StatusTypeDef st7032_set_icon_bit(I2C_HandleTypeDef *hi2c, uint8_t address, uint8_t bit, uint8_t on)
{
  uint8_t data;

  if (hi2c == NULL)
  {
    return HAL_ERROR;
  }

  data = 0x00U;
  if (on != 0U)
  {
    data = bit;
  }
  return st7032_write_icon(hi2c, address, data);
}

HAL_StatusTypeDef ST7032_SetIconS56(I2C_HandleTypeDef *hi2c, uint8_t on)
{
  return st7032_set_icon_bit(hi2c, ST7032_ICON_S56_ADDR, ST7032_ICON_S56_BIT, on);
}

HAL_StatusTypeDef ST7032_SetIconS37(I2C_HandleTypeDef *hi2c, uint8_t on)
{
  return st7032_set_icon_bit(hi2c, ST7032_ICON_S37_ADDR, ST7032_ICON_S37_BIT, on);
}

HAL_StatusTypeDef ST7032_SetIconS1(I2C_HandleTypeDef *hi2c, uint8_t on)
{
  return st7032_set_icon_bit(hi2c, ST7032_ICON_S1_ADDR, ST7032_ICON_S1_BIT, on);
}

HAL_StatusTypeDef ST7032_SetCursor(I2C_HandleTypeDef *hi2c, uint8_t address)
{
  return st7032_command(hi2c, (uint8_t)(ST7032_CMD_DDRAM | address));
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
