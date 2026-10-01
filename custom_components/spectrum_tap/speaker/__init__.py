import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import speaker
from esphome.const import CONF_ID, CONF_OUTPUT_SPEAKER

spectrum_tap_ns = cg.esphome_ns.namespace("spectrum_tap")
SpectrumTapSpeaker = spectrum_tap_ns.class_(
    "SpectrumTapSpeaker", speaker.Speaker, cg.Component
)

CONFIG_SCHEMA = (
    speaker.SPEAKER_SCHEMA.extend(
        {
            cv.Required(CONF_ID): cv.declare_id(SpectrumTapSpeaker),
            cv.Required(CONF_OUTPUT_SPEAKER): cv.use_id(speaker.Speaker),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await speaker.register_speaker(var, config)
    await cg.register_component(var, config)

    output = await cg.get_variable(config[CONF_OUTPUT_SPEAKER])
    cg.add(var.set_output_speaker(output))
