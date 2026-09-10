import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
import esphome.final_validate as fv
from esphome.components import climate, uart, text_sensor, binary_sensor, sensor
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "uart"]
AUTO_LOAD = ["text_sensor", "binary_sensor", "sensor"]
ns = cg.esphome_ns.namespace("bc7215a")
BC7215AClimate = ns.class_("BC7215AClimate", cg.Component, climate.Climate, uart.UARTDevice)

def validate_pins(config):
    for key in ("mod_pin", "busy_pin"):
        if config[key].get("inverted", False):
            raise cv.Invalid("BC7215A MOD/BUSY must not be inverted")
    return config


def validate_single(config):
    count = sum(c.get("platform") == "bc7215a" for c in fv.full_config.get().get("climate", []))
    if count > 1:
        raise cv.Invalid("This vendor library supports one BC7215A climate per ESP32")
    return config

CONFIG_SCHEMA = cv.All(
    climate.climate_schema(BC7215AClimate).extend({
        cv.Required("mod_pin"): pins.internal_gpio_output_pin_schema,
        cv.Required("busy_pin"): pins.internal_gpio_input_pin_schema,
        cv.Optional("temperature_sensor"): cv.use_id(sensor.Sensor),
        cv.Optional("humidity_sensor"): cv.use_id(sensor.Sensor),
        cv.Optional("status"): text_sensor.text_sensor_schema(),
        cv.Optional("match_info"): text_sensor.text_sensor_schema(),
        cv.Optional("pairing_id"): text_sensor.text_sensor_schema(),
        cv.Optional("library_version"): text_sensor.text_sensor_schema(),
        cv.Optional("vertical_swing_status"): text_sensor.text_sensor_schema(),
        cv.Optional("horizontal_swing_status"): text_sensor.text_sensor_schema(),
        cv.Optional("swing_state"): text_sensor.text_sensor_schema(),
        cv.Optional("paired"): binary_sensor.binary_sensor_schema(),
        cv.Optional("state_known"): binary_sensor.binary_sensor_schema(),
    }).extend(uart.UART_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_arduino, validate_pins,
)

FINAL_VALIDATE_SCHEMA = cv.All(validate_single, uart.final_validate_device_schema(
    "bc7215a", baud_rate=19200, require_tx=True, require_rx=True,
    data_bits=8, parity="NONE", stop_bits=2,
))

async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    for name in ("temperature_sensor", "humidity_sensor"):
        if name in config:
            entity = await cg.get_variable(config[name])
            cg.add(getattr(var, "set_" + name)(entity))
    for name in ("mod_pin", "busy_pin"):
        pin = await cg.gpio_pin_expression(config[name])
        cg.add(getattr(var, "set_" + name)(pin))
    for name in ("status", "match_info", "pairing_id", "library_version", "vertical_swing_status", "horizontal_swing_status", "swing_state", "paired", "state_known"):
        if name in config:
            entity = await (text_sensor.new_text_sensor(config[name]) if name in ("status", "match_info", "pairing_id", "library_version", "vertical_swing_status", "horizontal_swing_status", "swing_state")
                            else binary_sensor.new_binary_sensor(config[name]))
            cg.add(getattr(var, "set_" + name)(entity))
