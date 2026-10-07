#include "spectrum_viz.h"

#include <math.h>

// 16 logarithmic power bands produced by custom_components/spectrum_tap
// (128-point FFT @ 48 kHz -> ~375 Hz - 18 kHz, power-domain magnitude).
// Global symbol defined in spectrum.cpp; declared here for this TU (same
// pattern the old YAML lambda used - links against the same binary).
extern float spec_bands[16];

namespace esphome {
namespace spectrum_viz {

static const char *const TAG = "spectrum_viz";

bool SpectrumViz::resolve_bars() {
  bool ready = true;
  for (int i = 0; i < NUM_BARS; i++) {
    if (bars_[i] != nullptr)
      continue;
    if (bar_getters_[i])
      bars_[i] = bar_getters_[i]();
    if (bars_[i] == nullptr)
      ready = false;
  }
  return ready;
}

void SpectrumViz::set_bar_height(int index, int height) {
  if (index < 0 || index >= NUM_BARS || bars_[index] == nullptr)
    return;
  // Bottom-anchored: fixed x (15 + b*28), y grows upward (strip 472x75,
  // 3 px top pad). Same geometry as the original YAML lambda.
  lv_obj_set_pos(bars_[index], 15 + index * 28, 75 - height - 3);
  lv_obj_set_height(bars_[index], height);
}

void SpectrumViz::tick() {
  // Widget globals are assigned by LVGL init during setup; before that
  // (or if a bar id is missing) just skip the frame.
  if (!resolve_bars())
    return;

  // ── TEMPORARY validation log (remove once the new dB scale is confirmed) ──
  // Prints the 16 rendered bar heights once per LOG_INTERVAL_TICKS ticks
  // (40 ms/tick -> 5 s) and only while the animation interval is actually
  // running, so the log stays quiet. Expected spread after the dB fix on
  // typical music: lows ~40-70 px, highs ~10-40 px; nothing pinned at 70.
  constexpr uint32_t LOG_INTERVAL_TICKS = 125;   // 125 * 40 ms = 5 s
  constexpr float WINDOW_COHERENT_GAIN = 64.0f;  // sum of the Hann window (N=128)
  log_tick_++;
  bool should_log = (log_tick_ >= LOG_INTERVAL_TICKS);
  if (should_log)
    log_tick_ = 0;

  for (int b = 0; b < NUM_BARS; b++) {
    // Normalize the raw FFT power into a per-band RMS amplitude (0..~1):
    //   |X_k| of a full-scale sine == A/2 * sum(Hann) ~= A * 64
    float amp = sqrtf(spec_bands[b]) / WINDOW_COHERENT_GAIN;

    // dBFS compression mapped linearly over a 50 dB range: 0 dBFS -> 70 px,
    // -50 dBFS -> 2 px. Keeps the energy-heavy low bands from pinning while
    // the quieter high bands stay visible (fix for "first half always max").
    float db = 20.0f * log10f(amp + 1e-5f);
    float target = 70.0f * (1.0f - db / -50.0f);
    if (target < 2.0f)
      target = 2.0f;
    if (target > 70.0f)
      target = 70.0f;

    // Per-band attack/decay smoothing (fast rise, slow fall) - identical
    // tuning to the original lambda (attack 0.5, decay 0.85).
    if (target > viz_smooth_[b])
      viz_smooth_[b] = viz_smooth_[b] * 0.5f + target * 0.5f;
    else
      viz_smooth_[b] = viz_smooth_[b] * 0.85f;

    int h = (int) viz_smooth_[b];
    if (h < 2)
      h = 2;
    if (h > 70)
      h = 70;  // strip is 75 px tall, 3 px top pad
    set_bar_height(b, h);
    log_bands_[b] = (uint8_t) h;
  }

  if (should_log) {
    ESP_LOGI(TAG, "[VIZ] bars(px): %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u", (unsigned) log_bands_[0],
             (unsigned) log_bands_[1], (unsigned) log_bands_[2], (unsigned) log_bands_[3],
             (unsigned) log_bands_[4], (unsigned) log_bands_[5], (unsigned) log_bands_[6],
             (unsigned) log_bands_[7], (unsigned) log_bands_[8], (unsigned) log_bands_[9],
             (unsigned) log_bands_[10], (unsigned) log_bands_[11], (unsigned) log_bands_[12],
             (unsigned) log_bands_[13], (unsigned) log_bands_[14], (unsigned) log_bands_[15]);
  }
}

}  // namespace spectrum_viz
}  // namespace esphome