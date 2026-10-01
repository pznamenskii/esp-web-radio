"""SpectrumViz: LVGL bar-visualization driver for the spectrum_tap FFT.

Reads the 16 power bands published by custom_components/spectrum_tap
(spectrum.cpp, global `spec_bands[16]`) and animates the 16 visualizer
bars on the Now Playing page. Per-frame state (attack/decay smoothing,
bar pointers) lives in C++ members, so the ESPHome `interval:` only calls
`tick()` - no heavy lambdas in YAML, no `static` locals in generated code,
no `extern` declarations in YAML lambdas.

Why `cv.use_id(None)` for `bars:`: lvgl widget IDs are declared with
version-dependent C++ types (`lv_obj_t *` on modern ESPHome, typed
wrappers such as `esphome::lvgl::LvBarType *` on some releases - see
esphome/issues#6946). An untyped reference skips the ID type check, and
the C++ side resolves the underlying `lv_obj_t *` via
`spectrum_viz::detail::widget_to_lv_obj()` (handles both forms).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["lvgl"]
CODEOWNERS = ["@your_github"]

spectrum_viz_ns = cg.esphome_ns.namespace("spectrum_viz")
SpectrumViz = spectrum_viz_ns.class_("SpectrumViz", cg.Component)

CONF_BARS = "bars"
MAX_BARS = 16

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SpectrumViz),
        cv.Required(CONF_BARS): cv.All(
            cv.ensure_list(cv.use_id(None)), cv.Length(min=1, max=MAX_BARS)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    for i, bar_id in enumerate(config[CONF_BARS]):
        bar = await cg.get_variable(bar_id)
        # Bind each bar as a *getter* lambda rather than a resolved pointer:
        # the widget globals are assigned by LVGL's init code during setup,
        # and the binding call emitted here runs at the same time. The
        # component evaluates the getter lazily on its first tick() (interval
        # fires in loop(), always after setup), mirroring the static-cache
        # pattern the old YAML lambda used.
        # `widget_to_lv_obj` returns the underlying lv_obj_t* whether the id
        # is a plain `lv_obj_t *` or a typed wrapper exposing obj().
        getter = cg.RawExpression(
            f"[]() -> lv_obj_t * {{ "
            f"return spectrum_viz::detail::widget_to_lv_obj({bar}); "
            f"}}"
        )
        cg.add(var.set_bar_getter(i, getter))