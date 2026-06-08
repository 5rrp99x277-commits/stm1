/* scope.c — ADC1 + DMA + CMSIS-DSP FFT
 * MCU : STM32H743VIT6
 * Вход: PA3 = ADC1_IN15
 * Тайм: TIM6 TRGO → 20 кГц точно
 * DMA : DMA1 Stream 0 → AXI SRAM
 */
#include "scope.h"
#include <math.h>

/* ── Буферы в AXI SRAM (DMA2 видит без MPU трюков) ──────────── */
__attribute__((section(".axi_sram")))
uint16_t  adc_buf[ADC_N];

__attribute__((section(".axi_sram")))
float32_t fft_in[ADC_N];

__attribute__((section(".axi_sram")))
float32_t fft_mag[ADC_N / 2];

volatile uint8_t adc_ready = 0;

static arm_rfft_fast_instance_f32 fft_inst;
static ADC_HandleTypeDef hadc1;
static DMA_HandleTypeDef hdma_adc1;
static TIM_HandleTypeDef htim6;

/* ── TIM6: генерация TRGO @ 20 кГц ──────────────────────────── */
static void TIM6_Init(void) {
    __HAL_RCC_TIM6_CLK_ENABLE();
    htim6.Instance = TIM6;
    /* APB1 таймерный клок на H743 = 240 МГц при PLL 480 МГц */
    htim6.Init.Prescaler     = 0;
    htim6.Init.CounterMode   = TIM_COUNTERMODE_UP;
    htim6.Init.Period        = (240000000U / ADC_FS_HZ) - 1; /* 11999 */
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim6);

    TIM_MasterConfigTypeDef mc = {0};
    mc.MasterOutputTrigger = TIM_TRGO_UPDATE;
    mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim6, &mc);
    HAL_TIM_Base_Start(&htim6);
}

/* ── DMA1 Stream 0 для ADC1 ──────────────────────────────────── */
static void DMA_Init(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();
    hdma_adc1.Instance                 = DMA1_Stream0;
    hdma_adc1.Init.Request             = DMA_REQUEST_ADC1;
    hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode                = DMA_NORMAL;
    hdma_adc1.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_adc1);
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
}

/* ── ADC1: 16-bit, оверсэмплинг 4x, запуск от TIM6 ─────────── */
void ADC_Scope_Init(void) {
    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA3 → аналоговый вход */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    TIM6_Init();
    DMA_Init();

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV2;
    hadc1.Init.Resolution            = ADC_RESOLUTION_16B;
    hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait      = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIG_T6_TRGO;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_ONESHOT;
    hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.LeftBitShift          = ADC_LEFTBITSHIFT_NONE;
    hadc1.Init.OversamplingMode      = ENABLE;
    hadc1.Init.Oversampling.Ratio              = ADC_OVERSAMPLING_RATIO_4;
    hadc1.Init.Oversampling.RightBitShift      = ADC_RIGHTBITSHIFT_2;
    hadc1.Init.Oversampling.TriggeredMode      = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
    hadc1.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;
    HAL_ADC_Init(&hadc1);

    /* Калибровка (важно для точности!) */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel      = ADC_CHANNEL_15;     /* PA3 = IN15 на H743 */
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLETIME_32CYCLES_5;
    ch.SingleDiff   = ADC_SINGLE_ENDED;
    ch.OffsetNumber = ADC_OFFSET_NONE;
    HAL_ADC_ConfigChannel(&hadc1, &ch);
}

/* ── DMA IRQ ─────────────────────────────────────────────────── */
void DMA1_Stream0_IRQHandler(void) {
    HAL_DMA_IRQHandler(&hdma_adc1);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h) {
    if (h->Instance == ADC1) {
        /* Инвалидируем кэш D-Cache перед чтением DMA-буфера */
        SCB_InvalidateDCache_by_Addr((uint32_t*)adc_buf,
                                     sizeof(adc_buf));
        adc_ready = 1;
    }
}

/* ── ADC counts → float + окно Ханна ────────────────────────── */
void ADC_ToFloat_Hann(void) {
    const float32_t scale = ADC_VREF / 65535.0f;
    for (uint32_t i = 0; i < ADC_N; i++) {
        float32_t hann = 0.5f * (1.0f - cosf(
            2.0f * (float32_t)M_PI * i / (ADC_N - 1)));
        /* Центрируем: убираем DC-смещение ~Vref/2 */
        fft_in[i] = ((float32_t)adc_buf[i] * scale - ADC_VREF / 2.0f)
                    * hann;
    }
}

/* ── FFT → амплитуды в дБ ───────────────────────────────────── */
void FFT_Init(void) {
    arm_rfft_fast_init_f32(&fft_inst, ADC_N);
}

void FFT_Compute(void) {
    static float32_t tmp[ADC_N];
    arm_rfft_fast_f32(&fft_inst, fft_in, tmp, 0);
    arm_cmplx_mag_f32(tmp, fft_mag, ADC_N / 2);

    /* Нормировка + дБ */
    for (uint32_t i = 0; i < ADC_N / 2; i++) {
        float32_t v = fft_mag[i] * 2.0f / ADC_N;
        fft_mag[i] = (v < 1e-7f) ? -80.0f :
                     20.0f * log10f(v);
    }
}

/* ── Пиковая частота ─────────────────────────────────────────── */
float32_t FFT_PeakHz(void) {
    uint32_t  peak_bin = 1;
    float32_t peak_val = fft_mag[1];
    for (uint32_t i = 2; i < ADC_N / 2; i++) {
        if (fft_mag[i] > peak_val) {
            peak_val = fft_mag[i];
            peak_bin = i;
        }
    }
    return (float32_t)peak_bin * ADC_FS_HZ / ADC_N;
}

/* ── THD ────────────────────────────────────────────────────── */
float32_t FFT_THD_pct(void) {
    /* Ищем фундаментальный бин */
    uint32_t  fund_bin = 1;
    float32_t fund_val = fft_mag[1];
    for (uint32_t i = 2; i < ADC_N / 2; i++) {
        if (fft_mag[i] > fund_val) {
            fund_val = fft_mag[i];
            fund_bin = i;
        }
    }
    float32_t fund_lin = powf(10.0f, fund_val / 20.0f);
    float32_t harm_sum = 0.0f;
    for (uint32_t h = 2; h <= 8; h++) {
        uint32_t hb = fund_bin * h;
        if (hb >= ADC_N / 2) break;
        float32_t hv = powf(10.0f, fft_mag[hb] / 20.0f);
        harm_sum += hv * hv;
    }
    return 100.0f * sqrtf(harm_sum) / (fund_lin + 1e-10f);
}
