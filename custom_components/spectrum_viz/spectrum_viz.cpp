#include "spectrum_viz.h"

#include <math.h>

// 16 logarithmic power bands produced by custom_components/spectrum_tap
// (128-point FFT @ 48 kHz -> ~375 Hz - 18 kHz, power-domain magnitude).
// Global symbol defined in spectrum.cpp; declared here for this TU (same
// pattern the old YAML lambda used - links against the same binary).
extern float spec_bands[16];

namespace esphome {
namespace spectrum_viz {

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

  for (int b = 0; b < NUM_BARS; b++) {
    // Per-band attack/decay smoothing (fast rise, slow fall) - identical
    // tuning to the original lambda (GAIN 300.0f, attack 0.5, decay 0.85).
    float target = sqrtf(spec_bands[b]) * 300.0f;
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
  }
}

}  // namespace spectrum_viz
}  // namespace esphome