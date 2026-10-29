#include "portal.h"
#include "esphome/core/application.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/components/json/json_util.h"

namespace esphome::bc7215a_web {
bool Portal::canHandle(AsyncWebServerRequest *request) const {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(buf);
  return url == "/" || url == "/0.js" || url == "/0.css" ||
         url == "/provision/config" || url == "/provision/save" ||
         url == "/wifi" || url == "/generate_204" || url == "/hotspot-detect.html" ||
         url == "/connecttest.txt" || url == "/ncsi.txt";
}
void Portal::handleRequest(AsyncWebServerRequest *request) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(buf);
  if (url == "/" || url == "/0.js" || url == "/0.css") {
    server_->handleRequest(request);
    return;
  }
  if (url == "/provision/config" && request->method() == HTTP_GET) {
    // Read cached scans: active scanning while AP clients provision can disrupt
    // their connection. The initial full scan is retained by code generation.
    json::JsonBuilder builder;
    auto root = builder.root();
    root["local_url"] = "http://" + App.get_name() + ".local/";
    root["connected"] = wifi::global_wifi_component->is_connected();
    auto aps = root["aps"].to<JsonArray>();
    {
      wifi::ScanResultsLock lock(wifi::global_wifi_component);
      unsigned count = 0;
      for (const auto &scan : wifi::global_wifi_component->get_scan_result()) {
        if (scan.get_is_hidden() || count >= 24) continue;
        auto ap = aps.add<JsonObject>();
        ap["ssid"] = scan.get_ssid();
        ap["rssi"] = scan.get_rssi();
        ap["lock"] = scan.get_with_auth();
        count++;
      }
    }
    auto body = builder.serialize();
    auto *response = request->beginResponse(200, "application/json", body.c_str());
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
    return;
  }
  if (url == "/provision/save") {
    // Require a same-origin JS request; never put credentials in a URL or log.
    const auto marker = request->get_header("X-BC7215-Request");
    if (request->method() != HTTP_POST || !marker.has_value() || *marker != "1") {
      request->send(403, "text/plain", "Use the device Wi-Fi form"); return;
    }
    const auto origin = request->get_header("Origin");
    const auto host = request->get_header("Host");
    if (origin.has_value() && (!host.has_value() || *origin != "http://" + *host)) {
      request->send(403); return;
    }
    const auto ssid = request->arg("ssid");
    const auto password = request->arg("psk");
    if (ssid.empty() || ssid.size() > 32 || password.size() > 64 ||
        (!password.empty() && password.size() < 8)) {
      request->send(400, "text/plain", "Invalid SSID or password length"); return;
    }
    this->set_timeout("save_wifi", 300, [ssid, password]() {
      auto *network = wifi::global_wifi_component;
      if (network->is_ap_active()) {
        // Keep the provisioning AP alive if the new credentials are wrong.
        // Start before saving so save_wifi_sta()'s cooldown shortcut cannot
        // launch a second connection attempt in the normal success path.
        wifi::WiFiAP station;
        station.set_ssid(ssid.c_str());
        station.set_password(password.c_str());
        network->set_sta(station);
        network->start_connecting(station);
        network->save_wifi_sta(ssid.c_str(), password.c_str());
      } else {
        // Match ESPHome's wifi.configure lifecycle. Merely saving credentials
        // while connected leaves the device on the previous network.
        network->disable();
        network->save_wifi_sta(ssid.c_str(), password.c_str());
        network->enable();
      }
    });
    request->send(200, "application/json", "{\"accepted\":true}");
    return;
  }
  if (url == "/provision/config") { request->send(405); return; }
  auto *response = request->beginResponse(302, "text/plain", "Open the device dashboard");
  response->addHeader("Location", "/#wifi");
  request->send(response);
}
}
