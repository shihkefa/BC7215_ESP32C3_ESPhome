#pragma once

// The BC7215 vendor C++ wrapper only needs a byte stream, GPIO and time.
// Keep the original Arduino path intact; map those small interfaces to ESP-IDF
// when Matter requires the IDF framework.
#include "esphome/core/defines.h"
#ifdef USE_ARDUINO
#include <Arduino.h>
#include <Stream.h>
#else
#define BC7215_HAS_PACKET_STREAM
#include <cstddef>
#include <cstdint>
#include "esphome/core/hal.h"
#include "driver/gpio.h"

class Stream {
 public:
  virtual ~Stream() = default;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
  virtual size_t write(uint8_t value) = 0;
  virtual bool beginPacket() { return false; }
  virtual void endPacket() {}
  virtual bool packetBuffered() const { return false; }
};

constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int INPUT = 0;
constexpr int OUTPUT = 1;

inline void pinMode(int pin, int mode) {
  gpio_set_direction(static_cast<gpio_num_t>(pin),
                     // The vendor driver reads MOD back to select RX/TX parsing.
                     // IDF returns zero for output-only pads. Arduino OUTPUT
                     // keeps input enabled; preserve that behavior here.
                     mode == OUTPUT ? GPIO_MODE_INPUT_OUTPUT : GPIO_MODE_INPUT);
}
inline void digitalWrite(int pin, int level) {
  gpio_set_level(static_cast<gpio_num_t>(pin), level != LOW);
}
inline int digitalRead(int pin) { return gpio_get_level(static_cast<gpio_num_t>(pin)); }
using esphome::delay;
using esphome::micros;
using esphome::millis;
#endif
