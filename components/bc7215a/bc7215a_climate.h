#pragma once
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
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
  void log_trace();
  void drain_raw();
  uint32_t rx_count{0}, tx_count{0};
  uint32_t write_delta_us{0};
  bool io_error{false};
 private:
  uart::UARTComponent *bus_;
  uint8_t rx_trace_[256]{}, tx_trace_[256]{};
  size_t rx_size_{0}, tx_size_{0};
  uint32_t rx_dropped_{0}, tx_dropped_{0}, last_write_us_{0};
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
  bool swing_from_remote{false};
  int swing{-1}; // -1: remote/unknown; 0: both off; 1: vertical; 2: horizontal
};

// Separate preference: the existing PairingData layout and key remain unchanged.
struct SwingSample {
  uint8_t status{0};
  bc7215DataMaxPkt_t data{};
};
struct SwingAxis {
  bool ready{false};
  SwingSample off{}, on{};
};
struct SwingCanonical {
  bc7215DataMaxPkt_t data{};
  bc7215FormatPkt_t format{};
};
struct SwingData {
  uint32_t magic{0}, pairing{0};
  SwingAxis axes[2]{};
};

class BC7215AClimate : public Component, public climate::Climate, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void set_mod_pin(InternalGPIOPin *pin) { mod_ = pin; }
  void set_busy_pin(InternalGPIOPin *pin) { busy_ = pin; }
  void set_status(text_sensor::TextSensor *s) { status_ = s; }
  void set_match_info(text_sensor::TextSensor *s) { match_info_ = s; }
  void set_pairing_id(text_sensor::TextSensor *s) { pairing_id_ = s; }
  void set_library_version(text_sensor::TextSensor *s) { version_ = s; }
  void set_paired(binary_sensor::BinarySensor *s) { paired_ = s; }
  void set_state_known(binary_sensor::BinarySensor *s) { known_sensor_ = s; }
  void set_vertical_swing_status(text_sensor::TextSensor *s) { vertical_swing_status_ = s; }
  void set_horizontal_swing_status(text_sensor::TextSensor *s) { horizontal_swing_status_ = s; }
  void set_swing_state(text_sensor::TextSensor *s) { swing_state_ = s; }
  void start_swing_learning(bool horizontal);
  void arm_swing_sample();
  void cancel_swing_learning();
  bool is_learning_swing() const;
  void start_pairing();
  void next_match();
  void cancel_pairing();
  void retry_communication();
  bool is_pairing() const;
 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
  enum Phase { PROBE, PAIR, EXTRA, LISTEN, TX_ON, TX_SETTINGS, TX_OFF, FAULT,
               PROBE_WAKE, PROBE_TX_WAIT, UNPAIRED, LEARN_WAIT, LEARN_CAPTURE } phase_{PROBE};
  void status_message_(const char *message);
  void fault_(const char *message);
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
  void publish_settings_();
  void publish_match_();
  bool count_candidates_();
  void begin_command_();
  void send_settings_();
  void finish_command_();
  void load_swing_();
  void publish_swing_();
  void swing_prompt_();
  void learn_swing_received_();
  bool replace_swing_sample_(const SwingSample &sample);
  bool swing_available_(int mode) const;
  bool prepare_swing_();
  void sync_remote_swing_();
  bool canonical_swing_(const SwingSample &sample, SwingCanonical &out);
  bool same_swing_(const SwingCanonical &a, const SwingCanonical &b) const;
  InternalGPIOPin *mod_{nullptr}, *busy_{nullptr};
  UARTStream *stream_{nullptr};
  BC7215 *driver_{nullptr};
  BC7215AC *ac_{nullptr};
  text_sensor::TextSensor *status_{nullptr}, *match_info_{nullptr}, *version_{nullptr};
  text_sensor::TextSensor *pairing_id_{nullptr};
  binary_sensor::BinarySensor *paired_{nullptr}, *known_sensor_{nullptr};
  ESPPreferenceObject pref_, swing_pref_;
  SwingData swings_{};
  SwingAxis learning_axis_{};
  SwingSample received_swing_{};
  SwingCanonical canonical_rx_{}, canonical_off_{}, canonical_on_{};
  Capture learning_capture_{}; // Member, not a large ESP32 task-stack allocation.
  Settings learning_settings_{};
  unsigned learning_direction_{0}, learning_step_{0};
  uint32_t learning_since_{0};
  bool base_needs_restore_{false};
  text_sensor::TextSensor *vertical_swing_status_{nullptr}, *horizontal_swing_status_{nullptr};
  text_sensor::TextSensor *swing_state_{nullptr};
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
