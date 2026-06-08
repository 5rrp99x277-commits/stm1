#pragma once
#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <string.h>

/* ── Конфигурация ─────────────────────────────────────────────── */
#define SSD1306_I2C_ADDR    0x78   /* 0x3C << 1  (SA0=GND) */
#define SSD1306_W           128
#define SSD1306_H           64
#define SSD1306_PAGES       (SSD1306_H / 8)   /* 8 */

/* ── Буфер кадра ─────────────────────────────────────────────── */
extern uint8_t ssd1306_fb[SSD1306_PAGES][SSD1306_W];

/* ── API ──────────────────────────────────────────────────────── */
HAL_StatusTypeDef SSD1306_Init(I2C_HandleTypeDef *hi2c);
void SSD1306_Flush(void);            /* FB → дисплей по I2C DMA */
void SSD1306_Clear(void);
void SSD1306_SetPixel(uint8_t x, uint8_t y, uint8_t on);
void SSD1306_DrawHLine(uint8_t x0, uint8_t x1, uint8_t y);
void SSD1306_DrawVLine(uint8_t x, uint8_t y0, uint8_t y1);
void SSD1306_DrawChar(uint8_t x, uint8_t page, char c);
void SSD1306_DrawString(uint8_t x, uint8_t page, const char *str);
void SSD1306_DrawBar(uint8_t x, uint8_t y_top, uint8_t height);
