#pragma once
#include "status_led.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "bc7215ac.h"

namespace esphome::bc7215a {
// Adapt the existing vendor driver to ESPHome's UART (19200 8N2).
class UARTStream : public Stream {
 public:
  explicit UARTStream(uart::UARTComponent *bus) : bus_(bus) {}
  int available() override { return bus_->available(); }
  int read() override;
  int peek() override { uint8_t c; return bus_->available() && bus_->peek_byte(&c) ? c : -1; }
  void flush() override;
  size_t write(uint8_t c) override;
  void log_trace(bool force = false);
  void drain_raw();
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
  bool configure_cts(int pin);
  bool beginPacket() override;
  void endPacket() override;
  bool packetBuffered() const override { return buffering_; }
#endif
  uint32_t rx_count{0}, tx_count{0};
  uint32_t write_delta_us{0};
  bool io_error{false};
 private:
  uart::UARTComponent *bus_;
  uint8_t rx_trace_[256]{}, tx_trace_[256]{};
  size_t rx_size_{0}, tx_size_{0};
  uint32_t rx_dropped_{0}, tx_dropped_{0}, last_write_us_{0}, last_trace_ms_{0};
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
  int uart_num_{-1};
  bool buffering_{false};
  uint8_t packet_[1100]{}; // F5: 4-byte header + at most 512 stuffed data bytes.
  size_t packet_size_{0};
#endif
};

struct Capture {
  uint8_t count{0};
  uint8_t status[4]{};
  bc7215DataMaxPkt_t data[4]{};
  bc7215FormatPkt_t format[4]{};
};
struct PairingData {
  uint32_t magic{0};
  char version[24]{};
  uint8_t match{0};
  bool extra{false};
  Capture base{};
  Capture second{};
};
struct Settings {
  int temperature{25}, mode{1}, fan{0};
  bool power{false};
};

class BC7215AClimate : public Component, public climate::Climate, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void set_mod_pin(InternalGPIOPin *pin) { mod_ = pin; }
  void set_busy_pin(InternalGPIOPin *pin) { busy_ = pin; }
  void set_temperature_sensor(sensor::Sensor *s) { temperature_sensor_ = s; }
  void set_humidity_sensor(sensor::Sensor *s) { humidity_sensor_ = s; }
  void set_status(text_sensor::TextSensor *s) { status_ = s; }
  void set_match_info(text_sensor::TextSensor *s) { match_info_ = s; }
  void set_pairing_id(text_sensor::TextSensor *s) { pairing_id_ = s; }
  void set_library_version(text_sensor::TextSensor *s) { version_ = s; }
  void set_paired(binary_sensor::BinarySensor *s) { paired_ = s; }
  void set_state_known(binary_sensor::BinarySensor *s) { known_sensor_ = s; }
  void matter_set_system_mode(uint8_t mode);
  void matter_set_power(bool on);
  void matter_set_fan_mode(uint8_t value);
  uint8_t matter_fan_mode() const;
  void matter_set_target_temperature(int16_t centi);
  uint8_t matter_system_mode() const;
  uint8_t matter_running_mode() const;
  uint16_t matter_running_state() const;
  int16_t matter_target_temperature_centi() const;
  void start_pairing();
  void next_match();
  void cancel_pairing();
  void retry_communication();
  void recover_reception();
  bool is_pairing() const;
 protected:
  climate::ClimateTraits traits() override;
  void setup_environment_sensors_();
  void control(const climate::ClimateCall &call) override;
  enum Phase { PROBE, PAIR, EXTRA, LISTEN, TX_ON, TX_SETTINGS, TX_OFF, FAULT,
               PROBE_WAKE, PROBE_TX_WAIT, UNPAIRED } phase_{PROBE};
  void status_message_(const char *message);
  void fault_(const char *message);
  void recover_failed_tx_(const char *message);
  void begin_probe_();
  void log_diagnostics_();
  void listen_();
  void capture_(Capture &dst);
  bool valid_(const Capture &src);
  void apply_capture_(const Capture &src);
  bool apply_pairing_(const PairingData &src);
  bool apply_extra_(const Capture &src);
  void pair_complete_();
  void request_extra_();
  void received_();
  void arm_rx_watch_();
  void start_rx_recovery_();
  bool service_rx_recovery_();
  void service_rx_watch_();
  void publish_settings_();
  void publish_match_();
  bool count_candidates_();
  void begin_command_();
  bool matter_command_debounce_{false};
  uint32_t matter_command_since_{0};
  void send_settings_();
  void finish_command_();
  InternalGPIOPin *mod_{nullptr}, *busy_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr}, *humidity_sensor_{nullptr};
  uint32_t rx_parse_failures_{0}, rx_parse_failure_streak_{0};
  uint32_t rx_watch_since_{0}, rx_recovery_since_{0};
  uint32_t rx_activity_since_{0}, rx_activity_count_{0};
  uint32_t rx_monitor_since_{0}, rx_monitor_log_{0};
  bool rx_monitor_{false};
  uint32_t rx_rejected_{0};
  uint8_t rx_recovery_stage_{0}, rx_recovery_attempts_{0};
  bool rx_watch_{false};
  uint8_t rx_recovery_result_{0};
  UARTStream *stream_{nullptr};
  BC7215 *driver_{nullptr};
  BC7215AC *ac_{nullptr};
  text_sensor::TextSensor *status_{nullptr}, *match_info_{nullptr}, *version_{nullptr};
  text_sensor::TextSensor *pairing_id_{nullptr};
  binary_sensor::BinarySensor *paired_{nullptr}, *known_sensor_{nullptr};
  ESPPreferenceObject pref_;
  bool base_needs_restore_{false};
  PairingData saved_{}, candidate_{};
  Settings current_{}, desired_{}, active_{};
  bool paired_ok_{false}, known_{false}, pending_{false}, explicit_off_{false};
  uint8_t extra_type_{0};
  uint16_t candidate_count_{0};
  bool candidate_count_limited_{false};
  int key_{KEY_MODE}, active_key_{KEY_MODE};
  uint32_t since_{0};
  uint32_t probe_rx_start_{0}, probe_tx_start_{0}, diagnostic_since_{0};
};
}  // namespace esphome::bc7215a
