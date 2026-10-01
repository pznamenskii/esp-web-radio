#pragma once
#include <math.h>
#include <string.h>
#include <stdint.h>

static constexpr int FFT_N = 128;
static constexpr int VIZ_BANDS = 16;

// Доступно извне (для LVGL-отрисовки)
extern float spec_bands[VIZ_BANDS];

void spectrum_init();
bool spectrum_compute();
void feed_fft_samples(const uint8_t *data, int size);
