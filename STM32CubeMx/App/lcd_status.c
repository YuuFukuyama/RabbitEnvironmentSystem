/* 16 桁×2 行の環境表示を ST7032 に書く。 */

#include "lcd_status.h"
#include "env_judge.h"
#include "st7032.h"

#define LCD_LINE0 0x00U
#define LCD_LINE1 0x40U
#define LCD_COLS  16U

/* 16 桁以内で文字列を継ぎ足す。 */
static uint8_t copy_text(char *dst, uint8_t pos, const char *text)
{
  while ((*text != '\0') && (pos < LCD_COLS))
  {
    dst[pos] = *text;
    pos++;
    text++;
  }
  return pos;
}

/* 99 以下の整数を桁として継ぎ足す。 */
static uint8_t append_u8(char *dst, uint8_t pos, uint8_t value)
{
  if (value >= 100U)
  {
    value = 99U;
  }
  if ((value >= 10U) && (pos < LCD_COLS))
  {
    dst[pos] = (char)('0' + (value / 10U));
    pos++;
  }
  if (pos < LCD_COLS)
  {
    dst[pos] = (char)('0' + (value % 10U));
    pos++;
  }
  return pos;
}

/* 符号なし整数を継ぎ足す。 */
static uint8_t append_u16(char *dst, uint8_t pos, uint16_t value)
{
  char digits[5];
  uint8_t count;
  uint8_t index;

  if (value == 0U)
  {
    return append_u8(dst, pos, 0U);
  }

  count = 0U;
  while ((value > 0U) && (count < 5U))
  {
    digits[count] = (char)('0' + (value % 10U));
    count++;
    value = (uint16_t)(value / 10U);
  }

  index = count;
  while ((index > 0U) && (pos < LCD_COLS))
  {
    index--;
    dst[pos] = digits[index];
    pos++;
  }
  return pos;
}

/* 0.1 単位の値を小数 1 桁で継ぎ足す。 */
static uint8_t append_deci(char *dst, uint8_t pos, int16_t deci)
{
  uint16_t magnitude;

  if (deci < 0)
  {
    if (pos < LCD_COLS)
    {
      dst[pos] = '-';
      pos++;
    }
    deci = (int16_t)(-deci);
  }

  magnitude = (uint16_t)deci;
  pos = append_u16(dst, pos, (uint16_t)(magnitude / 10U));
  if (pos < LCD_COLS)
  {
    dst[pos] = '.';
    pos++;
  }
  if (pos < LCD_COLS)
  {
    dst[pos] = (char)('0' + (magnitude % 10U));
    pos++;
  }
  return pos;
}

/* 温度を T:22.5C の形で継ぎ足す。 */
static uint8_t append_temp(char *dst, uint8_t pos, int16_t temp_deci_c)
{
  pos = copy_text(dst, pos, "T:");
  pos = append_deci(dst, pos, temp_deci_c);
  return copy_text(dst, pos, "C");
}

/* 湿度を H:52.3% の形で継ぎ足す。 */
static uint8_t append_hum(char *dst, uint8_t pos, int16_t hum_deci_rh)
{
  pos = copy_text(dst, pos, "H:");
  pos = append_deci(dst, pos, hum_deci_rh);
  return copy_text(dst, pos, "%");
}

/* 行の残りを空白で埋め、終端を付ける。 */
static void finish_line(char *line, uint8_t pos)
{
  while (pos < LCD_COLS)
  {
    line[pos] = ' ';
    pos++;
  }
  line[LCD_COLS] = '\0';
}

/* LCD 向けの危険度略称を返す。 */
static const char *risk_text_lcd(EnvRisk risk)
{
  if (risk == ENV_RISK_WARNING)
  {
    return "WARN";
  }
  if (risk == ENV_RISK_DANGER)
  {
    return "DANG";
  }
  if (risk == ENV_RISK_CRITICAL)
  {
    return "CRIT";
  }
  return "NORM";
}

/* 継続分を Xm の形で継ぎ足す。 */
static uint8_t append_elapsed_min(char *dst, uint8_t pos, const EnvView *view)
{
  if (view->show_minutes == 0U)
  {
    return pos;
  }
  pos = append_u8(dst, pos, view->minutes);
  return copy_text(dst, pos, "m");
}

/* 1 行目に略称と継続分を書く。 */
static void build_status_line(char *line, const EnvView *view)
{
  uint8_t pos;

  pos = 0U;
  if (view->sensor_error != 0U)
  {
    pos = copy_text(line, pos, "S.ERR");
  }
  else
  {
    pos = copy_text(line, pos, risk_text_lcd(view->risk));
  }

  if (view->show_minutes != 0U)
  {
    pos = copy_text(line, pos, " ");
    pos = append_u8(line, pos, view->minutes);
    pos = copy_text(line, pos, "m");
  }
  finish_line(line, pos);
}

/* NORMAL は上段に継続分のみ、下段に温湿度を並べる。 */
static void build_normal_lines(char *line0, char *line1, const EnvView *view)
{
  uint8_t pos;

  finish_line(line0, append_elapsed_min(line0, 0U, view));
  pos = append_temp(line1, 0U, view->temp_deci_c);
  pos = copy_text(line1, pos, " ");
  pos = append_hum(line1, pos, view->hum_deci_rh);
  finish_line(line1, pos);
}

/* 下段に温湿度を並べるか、NO DATA を書く。 */
static void build_value_line(char *line, const EnvView *view)
{
  uint8_t pos;

  pos = 0U;
  if ((view->sensor_error != 0U) && (view->has_value == 0U))
  {
    pos = copy_text(line, pos, "NO DATA");
    finish_line(line, pos);
    return;
  }

  pos = append_temp(line, pos, view->temp_deci_c);
  pos = copy_text(line, pos, " ");
  pos = append_hum(line, pos, view->hum_deci_rh);
  finish_line(line, pos);
}

/* 指定した DDRAM アドレスへ 1 行書く。 */
static HAL_StatusTypeDef write_line(I2C_HandleTypeDef *hi2c, uint8_t address, const char *text)
{
  HAL_StatusTypeDef status;

  status = ST7032_SetCursor(hi2c, address);
  if (status != HAL_OK)
  {
    return status;
  }
  return ST7032_WriteString(hi2c, text);
}

HAL_StatusTypeDef LcdStatus_Show(I2C_HandleTypeDef *hi2c, const EnvView *view)
{
  char line0[LCD_COLS + 1U];
  char line1[LCD_COLS + 1U];
  HAL_StatusTypeDef status;

  if ((hi2c == NULL) || (view == NULL))
  {
    return HAL_ERROR;
  }

  if (EnvView_IsAlert(view) == 0U)
  {
    build_normal_lines(line0, line1, view);
    status = ST7032_SetIconS56(hi2c, 0U);
  }
  else
  {
    build_status_line(line0, view);
    build_value_line(line1, view);
    status = ST7032_SetIconS56(hi2c, 1U);
  }
  if (status != HAL_OK)
  {
    return status;
  }

#if defined(ENV_SENSOR_USE_DUMMY)
  /* ダミー取得のあいだは下向き三角を点ける。警告の S56 とは別アドレス。 */
  status = ST7032_SetIconS37(hi2c, 1U);
  if (status != HAL_OK)
  {
    return status;
  }
#endif

  status = write_line(hi2c, LCD_LINE0, line0);
  if (status != HAL_OK)
  {
    return status;
  }
  return write_line(hi2c, LCD_LINE1, line1);
}

HAL_StatusTypeDef LcdStatus_ShowWifi(I2C_HandleTypeDef *hi2c, uint8_t connected)
{
  if (hi2c == NULL)
  {
    return HAL_ERROR;
  }
  return ST7032_SetIconS1(hi2c, connected);
}
