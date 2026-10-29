#pragma once
#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/web_server/web_server.h"

namespace esphome::bc7215a_web {
class Portal : public Component, public AsyncWebHandler {
 public:
  Portal(web_server_base::WebServerBase *base, web_server::WebServer *server) : base_(base), server_(server) {}
  // Register before the captive portal's catch-all, even if Wi-Fi setup
  // activates fallback AP before WebServer::setup() runs.
  float get_setup_priority() const override { return setup_priority::WIFI + 2.0f; }
  void setup() override { base_->add_handler(this); }
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
 protected:
  web_server_base::WebServerBase *base_;
  web_server::WebServer *server_;
};
}
