#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#include <functional>

// LVGL 9 public API. The include path is provided by the lvgl component;
// this component declares DEPENDENCIES = ["lvgl"] so it is always loaded.
#include "lvgl.h"

namespace esphome {
namespace spectrum_viz {

namespace detail {
// Extract the underlying lv_obj_t* from an LVGL widget id expression.
// Modern ESPHome declares widget ids as `lv_obj_t *`; some releases (e.g.
// 2025.4.x, esphome/issues#6946) exposed typed wrappers with an obj()
// accessor. Overload resolution picks the non-template for lv_obj_t* and
// the template for wrapper types - correct on both.
inline lv_obj_t *widget_to_lv_obj(lv_obj_t *obj) { return obj; }
template<typename T> lv_obj_t *widget_to_lv_obj(T *obj) { return obj->obj(); }

}  // namespace detail

/// Drives the 16-bar spectrum visualizer from spec_bands[] (spectrum_tap).
///
/// All per-frame state lives in members (no `static` locals, no YAML-side
/// heavy lambdas). `tick()` is invoked by the viz_animation interval at
/// 40 ms; the YAML guards (visualizer_enabled, widget visibility, lvgl
/// paused) remain on the ESPHome side.
class SpectrumViz : public Component {
 public:
  static constexpr int NUM_BARS = 16;

  /// Bind one visualizer bar. The getter is evaluated lazily on the first
  /// tick() so the widget globals (assigned by LVGL init during setup) are
  /// guaranteed to be valid before use.
  void set_bar_getter(int index, std::function<lv_obj_t *()> getter) {
    if (index >= 0 && index < NUM_BARS) {
      bar_getters_[index] = std::move(getter);
    }
  }

  /// Render one animation frame (called from the 40 ms YAML interval).
  void tick();

 protected:
  /// Resolve all bar getters into bars_ (idempotent, safe to call every tick).
  bool resolve_bars();
  /// Bottom-anchored height update for one bar; no-op while unbound.
  void set_bar_height(int index, int height);

  lv_obj_t *bars_[NUM_BARS] = {nullptr};
  std::function<lv_obj_t *()> bar_getters_[NUM_BARS];
  float viz_smooth_[NUM_BARS] = {0.0f};

  // TEMPORARY validation-log state (remove together with the [VIZ] log in
  // tick() once the new dB scale is confirmed): tick counter for rate limiting
  // (1 line per 5 s) and the last rendered bar heights for the log line.
  uint32_t log_tick_{0};
  uint8_t log_bands_[NUM_BARS] = {0};
};

}  // namespace spectrum_viz
}  // namespace esphome