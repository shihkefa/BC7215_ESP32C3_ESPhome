import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server_base, web_server, wifi
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID

DEPENDENCIES = ["web_server", "wifi"]
AUTO_LOAD = ["json"]
ns = cg.esphome_ns.namespace("bc7215a_web")
Portal = ns.class_("Portal", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Portal),
    cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
    cv.GenerateID("web_server_id"): cv.use_id(web_server.WebServer),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    parent = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    server = await cg.get_variable(config["web_server_id"])
    var = cg.new_Pvariable(config[CONF_ID], parent, server)
    await cg.register_component(var, config)
    wifi.request_wifi_scan_results()
    wifi.request_wifi_scan_results_lock()
