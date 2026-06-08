/* main.c — STM32H743VIT6 + SSD1306 0.96" I2C
 * Осциллограф + FFT на 128×64 OLED
 *
 * Разметка экрана (128×64):
 *  Y=0..29  — осциллограмма (30px)
 *  Y=30     — разделительная линия
 *  Y=31..55 — спектр FFT (25px)
 *  Y=56..63 — строка статуса (page 7): "Pk:xxxx Hz THD:xx%"
 *
 * Пины I2C1:
 *  PB8 = SCL   PB9 = SDA
 *  Подтяжка: 4.7 кОм на 3.3В (внешние или активировать внутренние)
 */

#include "main.h"
#include "ssd1306.h"
#include "scope.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── Глобалы ─────────────────────────────────────────────────── */
I2C_HandleTypeDef hi2c1;

/* ── Прототипы ───────────────────────────────────────────────── */
static void SystemClock_Config(void);
static void I2C1_Init(void);
static void Render_Scope(void);
static void Render_FFT(void);
static void Render_Status(void);

/* ── Константы отрисовки ─────────────────────────────────────── */
#define SCOPE_Y0      0
#define SCOPE_H       30
#define DIVIDER_Y     30
#define FFT_Y0        31
#define FFT_H         25
#define STATUS_PAGE   7     /* строка = страница 7, Y=56..63 */

/* ── main ────────────────────────────────────────────────────── */
int main(void) {
    HAL_Init();
    SystemClock_Config();   /* 480 МГц PLL */

    I2C1_Init();

    /* Инициализация периферии */
    FFT_Init();
    ADC_Scope_Init();
    SSD1306_Init(&hi2c1);

    /* Заставка 1.5 с */
    SSD1306_DrawString(20, 3, "STM32H7 Scope");
    SSD1306_DrawString(30, 4, "FFT Analyzer");
    SSD1306_Flush();
    HAL_Delay(1500);

    /* Запуск первого захвата */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, ADC_N);

    /* ── Главный цикл ──────────────────────────────────────── */
    while (1) {
        if (adc_ready) {
            adc_ready = 0;

            /* 1. Подготовить данные */
            ADC_ToFloat_Hann();
            FFT_Compute();

            /* 2. Отрисовать фреймбуфер */
            SSD1306_Clear();
            Render_Scope();
            SSD1306_DrawHLine(0, 127, DIVIDER_Y);
            Render_FFT();
            Render_Status();

            /* 3. Отправить на дисплей */
            SSD1306_Flush();

            /* 4. Следующий захват */
            HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, ADC_N);
        }

        /* Можно добавить обработку кнопок / sleep */
    }
}

/* ── Отрисовка осциллограммы ─────────────────────────────────── */
static void Render_Scope(void) {
    /* Найти мин/макс для авто-масштаба */
    uint16_t vmin = adc_buf[0], vmax = adc_buf[0];
    for (uint32_t i = 1; i < ADC_N; i++) {
        if (adc_buf[i] < vmin) vmin = adc_buf[i];
        if (adc_buf[i] > vmax) vmax = adc_buf[i];
    }
    uint32_t range = (uint32_t)(vmax - vmin);
    if (range < 100) range = 100; /* минимальный масштаб */

    /* Рисуем 128 точек, прореживая ADC_N → 128 */
    uint8_t prev_y = SCOPE_Y0 + SCOPE_H / 2;
    for (uint8_t x = 0; x < 128; x++) {
        uint32_t idx = (uint32_t)x * ADC_N / 128;
        uint8_t y = SCOPE_Y0 + SCOPE_H - 1
                    - (uint8_t)(((uint32_t)(adc_buf[idx] - vmin)
                    * (SCOPE_H - 1)) / range);
        /* Соединяем точки вертикальной линией (без разрывов) */
        if (x == 0) {
            SSD1306_SetPixel(x, y, 1);
        } else {
            uint8_t y0 = (prev_y < y) ? prev_y : y;
            uint8_t y1 = (prev_y > y) ? prev_y : y;
            SSD1306_DrawVLine(x, y0, y1);
        }
        prev_y = y;
    }

    /* Пунктирная нулевая линия (средина) */
    uint8_t mid = SCOPE_Y0 + SCOPE_H / 2;
    for (uint8_t x = 0; x < 128; x += 4)
        SSD1306_SetPixel(x, mid, 1);
}

/* ── Отрисовка спектра FFT ───────────────────────────────────── */
static void Render_FFT(void) {
    /* Диапазон дБ: от -70 до 0 */
    const float32_t DB_MIN = -70.0f;
    const float32_t DB_MAX =   0.0f;
    const float32_t DB_RNG = DB_MAX - DB_MIN;

    /* Отображаем бины 1..127 → 128 колонок экрана */
    for (uint8_t x = 0; x < 128; x++) {
        uint32_t bin = 1 + (uint32_t)x * (ADC_N / 2 - 1) / 128;
        float32_t db = fft_mag[bin];
        if (db < DB_MIN) db = DB_MIN;
        if (db > DB_MAX) db = DB_MAX;

        uint8_t bar_h = (uint8_t)((db - DB_MIN) / DB_RNG * (FFT_H - 1));
        if (bar_h > 0)
            SSD1306_DrawBar(x, FFT_Y0 + FFT_H - bar_h, bar_h);
    }

    /* Сетка: вертикальные метки 5 кГц */
    /* 5000 Гц = бин 5000/(FS/N) = 5000*256/20000 = 64 бин → x = 64 */
    SSD1306_DrawVLine(64, FFT_Y0, FFT_Y0 + 3);   /* 5 кГц */
    SSD1306_DrawVLine(95, FFT_Y0, FFT_Y0 + 3);   /* 7.5 кГц */
}

/* ── Статусная строка ────────────────────────────────────────── */
static void Render_Status(void) {
    char buf[22];
    float32_t pk  = FFT_PeakHz();
    float32_t thd = FFT_THD_pct();

    if (pk >= 1000.0f)
        snprintf(buf, sizeof(buf), "%4.1fkHz THD%2.0f%%",
                 pk / 1000.0f, thd);
    else
        snprintf(buf, sizeof(buf), "%4.0fHz  THD%2.0f%%",
                 pk, thd);

    SSD1306_DrawString(0, STATUS_PAGE, buf);
}

/* ── I2C1 Init ───────────────────────────────────────────────── */
static void I2C1_Init(void) {
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB8=SCL, PB9=SDA, AF4, Open-Drain */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_PULLUP;   /* или внешние 4.7к */
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    hi2c1.Instance             = I2C1;
    hi2c1.Init.Timing          = 0x00702991; /* 400 кГц @ 240 МГц PCLK */
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);

    /* Включить аналоговый фильтр I2C (помехозащита) */
    HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);
}

/* ── Системный клок 480 МГц (внешний HSE 25 МГц) ────────────── */
static void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    /* 25 МГц HSE: M=5 → 5 МГц, N=192 → 960 МГц VCO, P=2 → 480 МГц */
    osc.PLL.PLLM       = 5;
    osc.PLL.PLLN       = 192;
    osc.PLL.PLLP       = 2;
    osc.PLL.PLLQ       = 4;   /* 240 МГц для USB и др. */
    osc.PLL.PLLR       = 2;
    osc.PLL.PLLFRACN   = 0;
    HAL_RCC_OscConfig(&osc);

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK
                  | RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1
                  | RCC_CLOCKTYPE_PCLK2   | RCC_CLOCKTYPE_D3PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;   /* HCLK  = 480 МГц */
    clk.AHBCLKDivider  = RCC_HCLK_DIV2;     /* HCLK  = 240 МГц (макс) */
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;     /* PCLK1 = 120 МГц */
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4);
}

/* ── Error handler ───────────────────────────────────────────── */
void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}
