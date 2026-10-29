#pragma once
#include <cstdint>

namespace esphome::bc7215a {
enum class StatusLedMode : uint8_t { WIFI_OFF, AP, REMOTE_PAIR, ONLINE };
inline StatusLedMode status_led_mode(bool pairing, bool ap, bool wifi) {
  if (pairing) return StatusLedMode::REMOTE_PAIR;
  if (ap) return StatusLedMode::AP;
  if (!wifi) return StatusLedMode::WIFI_OFF;
  // Matter commissioning does not change the RGB indicator.
  return StatusLedMode::ONLINE;
}
// RGB bit mask, 500 ms on / 500 ms off. No effects or transitions are mixed.
inline uint8_t status_led_color(StatusLedMode mode, uint32_t elapsed) {
  const bool blink = mode != StatusLedMode::WIFI_OFF && mode != StatusLedMode::ONLINE;
  if (blink && (elapsed / 500) % 2) return 0;
  switch (mode) {
    case StatusLedMode::WIFI_OFF: case StatusLedMode::AP: return 4;
    case StatusLedMode::REMOTE_PAIR: return 1;
    case StatusLedMode::ONLINE: return 2;
  }
  return 0;
}
}  // namespace esphome::bc7215a
