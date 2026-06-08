#pragma once
#include "stm32h7xx_hal.h"
#include "arm_math.h"

#define ADC_N        256      /* точек FFT — оптимально для 128px дисплея */
#define ADC_FS_HZ    20000U   /* 20 кГц дискретизация */
#define ADC_VREF     3.3f

/* Буферы (в AXI SRAM — DMA имеет прямой доступ) */
extern uint16_t  adc_buf[ADC_N];
extern float32_t fft_in [ADC_N];
extern float32_t fft_mag[ADC_N / 2];   /* амплитуды в дБ */

extern volatile uint8_t adc_ready;

void ADC_Scope_Init(void);
void ADC_ToFloat_Hann(void);            /* counts → float + окно */
void FFT_Init(void);
void FFT_Compute(void);                 /* → fft_mag[] в дБ     */

/* Результаты анализа */
float32_t FFT_PeakHz(void);
float32_t FFT_THD_pct(void);
