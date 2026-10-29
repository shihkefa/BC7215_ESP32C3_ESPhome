#include "bc7215a_climate.h"
#include "pairing_fingerprint.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
#include "esphome/components/uart/uart_component_esp_idf.h"
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "esp_heap_caps.h"
#endif

namespace esphome::bc7215a {
static const char *const TAG = "bc7215a";
int UARTStream::read() {
  uint8_t c;
  if (!bus_->available() || !bus_->read_byte(&c)) return -1;
  rx_count++;
  if (rx_size_ < sizeof(rx_trace_)) rx_trace_[rx_size_++] = c;
  else rx_dropped_++;
  return c;
}
size_t UARTStream::write(uint8_t c) {
  if (io_error) return 0;
  const uint32_t now = micros();
  write_delta_us = tx_count ? now - last_write_us_ : 0;
  last_write_us_ = now;
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
  if (buffering_) {
    if (packet_size_ == sizeof(packet_)) { io_error = true; return 0; }
    packet_[packet_size_++] = c;
  } else
#endif
  bus_->write_byte(c);
  tx_count++;
  if (tx_size_ < sizeof(tx_trace_)) tx_trace_[tx_size_++] = c;
  else tx_dropped_++;
  return 1;
}
void UARTStream::flush() {
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
  if (buffering_) return;
#endif
  if (io_error) return;
  io_error = bus_->flush() != uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS;
}
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
bool UARTStream::configure_cts(int pin) {
  auto *idf = static_cast<uart::IDFUARTComponent *>(bus_);
  uart_num_ = idf->get_hw_serial_number();
  const auto port = static_cast<uart_port_t>(uart_num_);
  if (uart_set_pin(port, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, pin) != ESP_OK ||
      uart_set_hw_flow_ctrl(port, UART_HW_FLOWCTRL_CTS, 0) != ESP_OK) {
    uart_num_ = -1;
    return false;
  }
  ESP_LOGI(TAG, "UART hardware CTS enabled: UART%d BUSY=GPIO%d", uart_num_, pin);
  return true;
}
bool UARTStream::beginPacket() {
  if (uart_num_ < 0) return false;
  packet_size_ = 0;
  buffering_ = true;
  return true;
}
void UARTStream::endPacket() {
  if (!buffering_) return;
  buffering_ = false;
  if (io_error || !packet_size_) { packet_size_ = 0; return; }
  const auto port = static_cast<uart_port_t>(uart_num_);
  const uint32_t started = millis();
  size_t sent = 0;
  // ESPHome installs this UART with no TX ring buffer. Use the nonblocking
  // FIFO API: a disconnected/stuck-high CTS can never block uart_write_bytes.
  while (sent < packet_size_ && millis() - started < 2000) {
    int n = uart_tx_chars(port, reinterpret_cast<const char *>(packet_ + sent), packet_size_ - sent);
    if (n < 0) { io_error = true; break; }
    sent += n;
    if (sent < packet_size_) delay(1);
  }
  const uint32_t remaining = 2000u - std::min<uint32_t>(2000u, millis() - started);
  if (sent != packet_size_ || uart_wait_tx_done(port, pdMS_TO_TICKS(remaining)) != ESP_OK) io_error = true;
  if (io_error) {
    // Only this component writes this UART; no queued TX ISR/ring buffer.
    // Discard unsent bytes before the receiver recovery toggles MOD.
    uart_ll_txfifo_rst(UART_LL_GET_HW(uart_num_));
    ESP_LOGW(TAG, "UART packet timeout: queued=%u/%u elapsed=%lu ms; FIFO discarded",
             unsigned(sent), unsigned(packet_size_), (unsigned long)(millis()-started));
  } else {
    ESP_LOGD(TAG, "UART packet completed: bytes=%u elapsed=%lu ms (hardware CTS)",
             unsigned(packet_size_), (unsigned long)(millis()-started));
  }
  packet_size_ = 0;
}
#endif
void UARTStream::drain_raw() {
  // Fault mode remains observable without feeding unpaired frames to the AC library.
  for (unsigned i = 0; i < 256 && available(); i++) if (read() < 0) break;
}
void UARTStream::log_trace(bool force) {
  // Batch debug output without delaying reads or parsing. Per-byte API log
  // messages compete with IR, Wi-Fi and Matter work on this single-core C3.
  const uint32_t now = millis();
  if (!force && now - last_trace_ms_ < 100 && rx_size_ < sizeof(rx_trace_) && tx_size_ < sizeof(tx_trace_)) return;
  last_trace_ms_ = now;
  auto dump = [](const char *direction, const uint8_t *data, size_t size, uint32_t dropped) {
    for (size_t offset = 0; offset < size; offset += 32) {
      char hex[32 * 3 + 1]{};
      const size_t count = std::min(size - offset, size_t(32));
      for (size_t i = 0; i < count; i++)
        snprintf(hex + i * 3, 4, "%02X ", unsigned(data[offset + i]));
      ESP_LOGD(TAG, "UART %s: %s", direction, hex);
    }
    if (dropped) ESP_LOGW(TAG, "UART %s trace omitted %lu bytes; parser data unaffected",
                         direction, (unsigned long) dropped);
  };
  // Defer logging until outside command writes; never add logging between F7 and 00.
  dump("TX", tx_trace_, tx_size_, tx_dropped_);
  dump("RX", rx_trace_, rx_size_, rx_dropped_);
  rx_size_ = tx_size_ = 0;
  rx_dropped_ = tx_dropped_ = 0;
}
static constexpr uint32_t MAGIC = 0xBC720103;
static constexpr climate::ClimateMode MODES[] = {
    climate::CLIMATE_MODE_AUTO, climate::CLIMATE_MODE_COOL,
    climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_FAN_ONLY};
static constexpr climate::ClimateFanMode FANS[] = {
    climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
    climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH};

climate::ClimateTraits BC7215AClimate::traits() {
  climate::ClimateTraits t;
  if (temperature_sensor_) t.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  if (humidity_sensor_) t.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY);
  t.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO,
      climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_FAN_ONLY});
  t.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH});
  t.set_visual_min_temperature(16);
  t.set_visual_max_temperature(30);
  t.set_visual_temperature_step(1);
  return t;
}

void BC7215AClimate::status_message_(const char *message) {
  ESP_LOGI(TAG, "%s", message);
  if (status_) status_->publish_state(message);
}
void BC7215AClimate::fault_(const char *message) {
  phase_ = FAULT;
  pending_ = false;
  known_ = false;
  paired_ok_ = false;
  if (paired_) paired_->publish_state(false);
  if (known_sensor_) known_sensor_->publish_state(false);
  // F7 may have succeeded even when its ACK was lost. Wake the module for raw RX.
  if (driver_) driver_->setRx();
  this->status_set_error(LOG_STR("BC7215A communication fault"));
  status_message_(message);
  log_diagnostics_();
}
void BC7215AClimate::recover_failed_tx_(const char *message) {
  // Never replay an uncertain IR command: a power toggle may already have
  // reached the appliance even if the completion byte was lost.
  ESP_LOGW(TAG, "%s; preserving pairing and recovering RX without IR retry", message);
  log_diagnostics_();
  pending_ = false;
  known_ = false;
  if (known_sensor_) known_sensor_->publish_state(false);
  driver_->io_error = stream_->io_error = false;
  phase_ = LISTEN;
  rx_watch_ = false;
  arm_rx_watch_();
  start_rx_recovery_();
}
void BC7215AClimate::log_diagnostics_() {
  if (!stream_) return;
  ESP_LOGI(TAG, "DIAG phase=%u MOD=%d BUSY=%d TX=%lu RX=%lu busy_error=%d uart_error=%d",
           unsigned(phase_), digitalRead(mod_->get_pin()), digitalRead(busy_->get_pin()),
           (unsigned long) stream_->tx_count, (unsigned long) stream_->rx_count,
           driver_->io_error, stream_->io_error);
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
  ESP_LOGD(TAG, "Memory: free=%u min_free=%u largest_block=%u bytes",
           unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
           unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
#endif
  stream_->log_trace(true);
}
void BC7215AClimate::begin_probe_() {
  rx_recovery_stage_ = 0;
  rx_watch_ = false;
  pending_ = false;
  paired_ok_ = known_ = false;
  if (paired_) paired_->publish_state(false);
  if (known_sensor_) known_sensor_->publish_state(false);
  driver_->io_error = stream_->io_error = false;
  driver_->setRx();
  phase_ = PROBE_WAKE;
  since_ = millis();
  this->status_clear_error();
  status_message_("檢查 BC7215A 通訊：先喚醒模組");
}
bool BC7215AClimate::is_pairing() const {
  return phase_ == PAIR || phase_ == EXTRA;
}
void BC7215AClimate::retry_communication() {
  // Retry cannot interrupt an in-flight AC command or an active pairing session.
  if (!driver_ || phase_ != FAULT) return;
  begin_probe_();
}
void BC7215AClimate::arm_rx_watch_() {
  rx_monitor_ = true;
  rx_monitor_since_ = millis();
  // Keep protection armed after interference, including after a valid frame.
  // Repeated noise must not postpone the timer.
  if (rx_watch_) return;
  rx_watch_ = true;
  rx_recovery_attempts_ = 0;
  rx_watch_since_ = millis();
  rx_activity_since_ = millis();
  rx_activity_count_ = stream_->rx_count;
}
void BC7215AClimate::recover_reception() {
  if (!ac_ || !driver_ || !stream_ || phase_ != LISTEN || !paired_ok_ || pending_ || rx_recovery_stage_) return;
  rx_watch_ = false; // Explicit button press may request immediate recovery.
  arm_rx_watch_();
  start_rx_recovery_();
}
void BC7215AClimate::start_rx_recovery_() {
  ac_->stopCapture();
  unsigned drained = 0;
  while (drained < 4096 && stream_->available() > 0) {
    if (stream_->read() < 0) break;
    drained++;
  }
  driver_->resetReceiveState();
  if (!apply_pairing_(candidate_)) {
    rx_watch_ = false;
    fault_("接收重整時恢復配對失敗；請重新檢查通訊");
    return;
  }
  base_needs_restore_ = true;
  driver_->abortCommand();
  rx_recovery_stage_ = 1;
  rx_recovery_since_ = millis();
  if (rx_recovery_attempts_ < 255) rx_recovery_attempts_++;
  ESP_LOGD(TAG, "RX recovery start: attempt=%u, automatic guard retained, abort=7A, drained=%u; no IR sent",
           rx_recovery_attempts_, drained);
  if (rx_recovery_attempts_ == 1)
    status_message_("接收異常，背景恢復中；保留最後有效狀態");
}
bool BC7215AClimate::service_rx_recovery_() {
  if (!rx_recovery_stage_) return false;
  if (rx_recovery_stage_ == 1) {
    // The datasheet requires >=36ms after the abort before another command.
    stream_->drain_raw();
    if (millis() - rx_recovery_since_ < 40) return true;
    driver_->resetReceiveState();
    driver_->setShutDown(); // F7 00 stops the module; it does not turn the AC off.
    rx_recovery_stage_ = 2;
    rx_recovery_since_ = millis();
    return true;
  }
  const bool ack = driver_->cmdCompleted();
  if (!ack && millis() - rx_recovery_since_ < 500) return true;
  if (ack) ESP_LOGD(TAG, "RX recovery UART ACK=7A; waking receiver (IR recovery unverified)");
  else ESP_LOGW(TAG, "RX recovery UART ACK timeout; waking receiver, automatic guard retained");
  driver_->resetReceiveState();
  ac_->startCapture(); // MOD high wakes the module, then restores composite mode.
  rx_recovery_stage_ = 0;
  rx_watch_since_ = millis();
  const uint8_t result = ack ? 1 : 2;
  if (rx_recovery_attempts_ == 1 || rx_recovery_result_ != result)
    status_message_(ack ? "模組已回覆；等待有效遙控訊號確認恢復" :
                          "模組未確認回覆；已嘗試喚醒，等待有效遙控訊號");
  rx_recovery_result_ = result;
  return true;
}
void BC7215AClimate::service_rx_watch_() {
  // Traffic is evidence the module is still producing data, not proof of a
  // valid AC command. Wait for quiet before interrupting reception. This also
  // lets a sequence of real remote presses finish before any recovery.
  if (stream_->rx_count != rx_activity_count_) {
    rx_activity_count_ = stream_->rx_count;
    rx_activity_since_ = millis();
  }
  // A half UART packet with no further bytes can strand the driver's busy
  // flag even after the previous AC command decoded successfully.
  if (phase_ == LISTEN && paired_ok_ && !pending_ && !rx_recovery_stage_ &&
      !rx_watch_ && driver_->receiveIncomplete() &&
      stream_->available() == 0 && millis() - rx_activity_since_ >= 1500) {
    ESP_LOGW(TAG, "RX stalled: incomplete UART packet with no progress; recovery armed");
    arm_rx_watch_();
  }
  if (rx_monitor_ && phase_ == LISTEN && !rx_recovery_stage_ &&
      millis() - rx_monitor_log_ >= 5000) {
    rx_monitor_log_ = millis();
    ESP_LOGI(TAG, "RX health: bytes=%lu quiet_ms=%lu partial=%d samples=%u watch=%d attempts=%u pending=%d",
             (unsigned long) stream_->rx_count, (unsigned long) (millis() - rx_activity_since_),
             driver_->receiveIncomplete(), unsigned(ac_->sampleCount), rx_watch_,
             rx_recovery_attempts_, pending_);
    log_diagnostics_();
    if (!rx_watch_ && millis() - rx_monitor_since_ >= 60000) rx_monitor_ = false;
  }
  if (!rx_watch_ || rx_recovery_stage_) return;
  if (!paired_ok_) { rx_watch_ = false; return; }
  if (phase_ != LISTEN) return; // Pause through TX and pairing, retain protection.
  if (pending_) return; // HA commands have priority; never interrupt a command.
  if (stream_->available() > 0 || millis() - rx_activity_since_ < 1500) return;
  const uint32_t wait_ms = rx_recovery_attempts_ == 0 ? 1500 : 5000;
  if (millis() - rx_watch_since_ >= wait_ms)
    start_rx_recovery_(); // Runs even when no new UART bytes arrive.
}

void BC7215AClimate::setup_environment_sensors_() {
  // Measurements only: never alter the setpoint, pairing, or IR command queue.
  current_temperature = NAN;
  current_humidity = NAN;
  if (temperature_sensor_) {
    auto update = [this](float value) {
      current_temperature = std::isfinite(value) ? value : NAN;
      this->publish_state();
    };
    temperature_sensor_->add_on_state_callback(update);
    update(temperature_sensor_->state);
  }
  if (humidity_sensor_) {
    auto update = [this](float value) {
      current_humidity = std::isfinite(value) && value >= 0.0f && value <= 100.0f ? value : NAN;
      this->publish_state();
    };
    humidity_sensor_->add_on_state_callback(update);
    update(humidity_sensor_->state);
  }
}

void BC7215AClimate::setup() {
  mod_->setup(); busy_->setup();
  stream_ = new UARTStream(parent_);
  driver_ = new BC7215(*stream_, mod_->get_pin(), busy_->get_pin());
  ac_ = new BC7215AC(*driver_);
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
  if (!stream_->configure_cts(busy_->get_pin())) {
    fault_("UART CTS 設定失敗；未啟動紅外通訊");
    return;
  }
#endif
  ac_->setCelsius();
  if (version_) version_->publish_state(ac_->getLibVer());
  if (match_info_) match_info_->publish_state("尚未配對");
  if (pairing_id_) pairing_id_->publish_state("尚未配對");
  pref_ = global_preferences->make_preference<PairingData>(MAGIC);
  pref_.load(&saved_);
  // Never transmit on startup. Keep the last displayed setpoint only.
  auto old = this->restore_state_();
  if (old.has_value()) {
    old->apply(this);
    if (std::isfinite(target_temperature) && target_temperature >= 16 && target_temperature <= 30)
      current_.temperature = lroundf(target_temperature);
    for (int i=0; i<5; i++) if (mode == MODES[i]) current_.mode = i;
    for (int i=0; i<4; i++) if (fan_mode.has_value() && *fan_mode == FANS[i]) current_.fan = i;
    current_.power = mode != climate::CLIMATE_MODE_OFF;
  }
  desired_ = current_;
  setup_environment_sensors_();
  publish_settings_();
  if (paired_) paired_->publish_state(false);
  // No AC IR is sent. F7 00 only shuts down the module itself during the probe.
  begin_probe_();
}

bool BC7215AClimate::valid_(const Capture &s) {
  if (s.count == 0 || s.count > 4) return false;
  for (unsigned i=0; i<s.count; i++)
    if (s.status[i] == 0xff || s.data[i].bitLen == 0 ||
        s.data[i].bitLen > sizeof(s.data[i].data)*8) return false;
  return true;
}
void BC7215AClimate::capture_(Capture &dst) {
  dst = Capture{};
  dst.count = ac_->sampleCount;
  for (unsigned i=0; i<dst.count && i<4; i++) {
    dst.status[i] = ac_->sampleStatus[i];
    dst.data[i] = ac_->sampleData[i];
    dst.format[i] = ac_->sampleFormat[i];
  }
}
void BC7215AClimate::apply_capture_(const Capture &s) {
  ac_->sampleCount = s.count;
  for (unsigned i=0; i<s.count; i++) {
    ac_->sampleStatus[i] = s.status[i];
    ac_->sampleData[i] = s.data[i];
    ac_->sampleFormat[i] = s.format[i];
  }
}
bool BC7215AClimate::apply_extra_(const Capture &s) {
  if (!valid_(s) || s.count != 1) return false;
  bc7215CombinedMsg_t msg{};
  msg.body.msg.fmt = &s.format[0];
  msg.body.msg.datPkt = reinterpret_cast<const bc7215DataVarPkt_t *>(&s.data[0]);
  return bc7215_ac_save_2nd_base(s.status[0], &msg);
}
bool BC7215AClimate::apply_pairing_(const PairingData &s) {
  if (s.magic != MAGIC || strncmp(s.version, ac_->getLibVer(), sizeof(s.version)) != 0 || !valid_(s.base))
    return false;
  apply_capture_(s.base);
  if (!ac_->init()) return false;
  for (unsigned i=0; i<s.match; i++) if (!ac_->matchNext()) return false;
  if (bc7215_ac_need_extra_sample() && (!s.extra || !apply_extra_(s.second))) return false;
  return true;
}
void BC7215AClimate::listen_() {
  const bool was_listening = phase_ == LISTEN;
  phase_ = LISTEN;
  if (was_listening) ac_->resumeCapture();
  else ac_->startCapture();
}
void BC7215AClimate::start_pairing() {
  if (rx_recovery_stage_) return;
  rx_watch_ = false;
  if (!ac_ || phase_ == PROBE || phase_ == PROBE_WAKE || phase_ == PROBE_TX_WAIT || phase_ == FAULT || phase_ == TX_ON ||
      phase_ == TX_SETTINGS || phase_ == TX_OFF) return;
  pending_ = false;
  paired_ok_ = false;
  known_ = false;
  if (match_info_) match_info_->publish_state("配對中");
  if (pairing_id_) pairing_id_->publish_state("配對中");
  if (paired_) paired_->publish_state(false);
  if (known_sensor_) known_sensor_->publish_state(false);
  ac_->stopCapture();
  candidate_ = PairingData{};
  phase_ = PAIR;
  ac_->startCapture();
  status_message_("配對中：遙控器設冷房25°C，對準接收頭按風速鍵");
}
void BC7215AClimate::request_extra_() {
  extra_type_ = bc7215_ac_need_extra_sample();
  if (!extra_type_) { pair_complete_(); return; }
  phase_ = EXTRA;
  ac_->startCapture();
  const char *msg = "需補充取樣：維持冷房25°C，再發送一次完整訊號";
  if (extra_type_ == 1) msg = "補充取樣：先調24°C，再對準接收頭按升溫回25°C";
  if (extra_type_ == 2) msg = "補充取樣：循環模式，最後切回冷房25°C";
  if (extra_type_ == 3) msg = "補充取樣：保持冷房25°C，按風速鍵";
  status_message_(msg);
}
void BC7215AClimate::pair_complete_() {
  candidate_.magic = MAGIC;
  strncpy(candidate_.version, ac_->getLibVer(), sizeof(candidate_.version)-1);
  paired_ok_ = true;
  if (paired_) paired_->publish_state(true);
  bool stored = pref_.save(&candidate_) && global_preferences->sync();
  if (stored) saved_ = candidate_;
  if (!count_candidates_()) return;
  publish_match_();
  if (!stored && pairing_id_) pairing_id_->publish_state("未保存");
  current_ = Settings{};
  current_.power = true; // User was explicitly instructed to send cool 25C.
  known_ = false;       // Require a subsequent parsed frame or completed command.
  desired_ = current_;
  publish_settings_();
  listen_();
  status_message_(stored ? "配對已保存；請用原廠遙控器調溫確認同步" : "配對成功但保存失敗；重開機需重配");
}
void BC7215AClimate::cancel_pairing() {
  if (!ac_ || (phase_ != PAIR && phase_ != EXTRA)) return;
  ac_->stopCapture();
  if (apply_pairing_(saved_)) {
    candidate_ = saved_;
    if (!count_candidates_()) return;
    publish_match_();
    paired_ok_ = true;
    if (paired_) paired_->publish_state(true);
    listen_();
    status_message_("已恢復上次配對；請用遙控器同步狀態");
  } else {
    // Cancellation without a saved pairing must actually leave pairing mode.
    phase_ = UNPAIRED;
    paired_ok_ = known_ = pending_ = false;
    driver_->setRx();
    if (paired_) paired_->publish_state(false);
    if (known_sensor_) known_sensor_->publish_state(false);
    if (match_info_) match_info_->publish_state("尚未配對");
    if (pairing_id_) pairing_id_->publish_state("尚未配對");
    status_message_("配對已取消；按開始配對可重新開始");
  }
}
void BC7215AClimate::next_match() {
  if (rx_recovery_stage_) return;
  rx_watch_ = false;
  if (!ac_ || phase_ != LISTEN || !paired_ok_ || !valid_(candidate_.base)) return;
  ac_->stopCapture();
  // candidate_ is unchanged until a new match succeeds; avoid a 4,420-byte
  // copy on the ESPHome main task stack just to restore the failure path.
  apply_capture_(candidate_.base);
  bool ok = ac_->init();
  for (unsigned i=0; ok && i<=candidate_.match; i++) ok = ac_->matchNext();
  if (ok && candidate_.match < 254) {
    candidate_.match++;
    if (match_info_) match_info_->publish_state("配對中");
    if (pairing_id_) pairing_id_->publish_state("配對中");
    candidate_.extra = false;
    paired_ok_ = false;
    pending_ = false;
    if (paired_) paired_->publish_state(false);
    request_extra_();
  } else {
    if (!apply_pairing_(candidate_)) { start_pairing(); return; }
    listen_(); status_message_("沒有下一個匹配，保留原匹配");
  }
}

bool BC7215AClimate::count_candidates_() {
  // The vendor library has global mutable matching state. Enumerate only while
  // capture is stopped, then restore the selected candidate and extra sample.
  candidate_count_ = 0;
  candidate_count_limited_ = false;
  apply_capture_(candidate_.base);
  if (ac_->init()) {
    candidate_count_ = 1;
    const uint32_t started = millis();
    while (ac_->matchNext()) {
      candidate_count_++;
      if (candidate_count_ >= 256 || millis() - started >= 500) {
        candidate_count_limited_ = true;
        break;
      }
    }
  }
  if (!apply_pairing_(candidate_)) {
    fault_("候選計數後恢復配對失敗；請重新檢查通訊");
    return false;
  }
  // A limited scan may stop before an already selected candidate.
  if (candidate_count_limited_)
    candidate_count_ = std::max(candidate_count_, uint16_t(candidate_.match + 1));
  return true;
}
void BC7215AClimate::publish_match_() {
  char value[80];
  if (candidate_count_)
    snprintf(value, sizeof(value), "候選 %02u／%s %02u 個", unsigned(candidate_.match)+1,
             candidate_count_limited_ ? "至少" : "共", unsigned(candidate_count_));
  else
    snprintf(value, sizeof(value), "候選 %02u／總數未知", unsigned(candidate_.match)+1);
  if (match_info_) match_info_->publish_state(value);
  if (pairing_id_) {
    snprintf(value, sizeof(value), "P-%08lX", (unsigned long) pairing_fingerprint(candidate_));
    pairing_id_->publish_state(value);
  }
}
void BC7215AClimate::publish_settings_() {
  target_temperature = current_.temperature;
  mode = current_.power ? MODES[current_.mode] : climate::CLIMATE_MODE_OFF;
  fan_mode = FANS[current_.fan];
  if (known_sensor_) known_sensor_->publish_state(known_);
  this->publish_state();
}
void BC7215AClimate::received_() {
  if (phase_ == PAIR) {
    capture_(candidate_.base); // Store all raw segments BEFORE library normalizes them.
    if (valid_(candidate_.base) && ac_->init()) request_extra_();
    else { ac_->startCapture(); status_message_("未匹配，請確認冷房25°C後再試"); }
    return;
  }
  if (phase_ == EXTRA) {
    capture_(candidate_.second);
    if (apply_extra_(candidate_.second)) { candidate_.extra = true; pair_complete_(); }
    else { ac_->startCapture(); status_message_("補充取樣失敗；請重試，必要時重新配對"); }
    return;
  }
  // A decoder ERR packet is not an AC state change. Do not feed it into the
  // vendor parser, which replaces its base before determining parse success.
  for (unsigned i=0; i<ac_->sampleCount && i<4; i++) {
    if (ac_->sampleStatus[i] & 0x80) {
      rx_rejected_++;
      arm_rx_watch_();
      ESP_LOGD(TAG, "RX rejected: ERR status=0x%02X bits=%u total=%lu; state preserved",
               ac_->sampleStatus[i], unsigned(ac_->sampleData[i].bitLen), (unsigned long) rx_rejected_);
      status_message_("忽略解碼錯誤訊號；保留最後有效狀態，接收監測中");
      listen_();
      return;
    }
  }
  int t=-1, m=-1, f=-1, p=-1;
  // Log metadata before parse(), which can normalize REV samples in place.
  ESP_LOGD(TAG, "RX input: segments=%u candidate=%u uptime=%lu ms", unsigned(ac_->sampleCount),
           unsigned(candidate_.match) + 1, (unsigned long) millis());
  for (unsigned i=0; i<ac_->sampleCount && i<4; i++)
    ESP_LOGD(TAG, "RX segment %u: status=0x%02X bits=%u signature=0x%02X", i+1,
             ac_->sampleStatus[i], unsigned(ac_->sampleData[i].bitLen),
             ac_->sampleFormat[i].signature.inByte);
  if (ac_->parse(t,m,f,p)) {
    if (rx_watch_) ESP_LOGI(TAG, "RX verified: valid remote frame; automatic recovery guard retained");
    if (rx_monitor_) rx_monitor_since_ = millis(); // Continue diagnostics after a valid frame.
    if (rx_parse_failure_streak_)
      ESP_LOGI(TAG, "RX parse resumed after %lu failed frames", (unsigned long) rx_parse_failure_streak_);
    rx_parse_failure_streak_ = 0;
    ESP_LOGI(TAG, "IR RX: temp=%d mode=%d fan=%d power=%d", t,m,f,p);
    if (t>=16 && t<=30) current_.temperature=t;
    if (m>=0 && m<5) current_.mode=m;
    if (f>=0 && f<4) current_.fan=f;
    if (p==0 || p==1) { current_.power=(p==1); known_=true; }
    else if (p==2 && known_) current_.power=!current_.power;
    const bool partial_settings = t < 16 || t > 30 || m < 0 || m >= 5 || f < 0 || f >= 4;
    if (partial_settings) known_ = false;
    if (!pending_) desired_=current_;
    publish_settings_();
    status_message_(p==2 && !known_ ? "收到切換電源碼，但起始電源狀態未知" :
                    partial_settings ? "收到遙控器訊號；部分設定無法辨識，未辨識欄位保留前值，接收繼續" :
                    "收到原廠遙控器訊號，已更新HA");
  } else {
    rx_parse_failures_++;
    rx_parse_failure_streak_++;
    base_needs_restore_ = true; // Failed replace_base may still damage vendor state.
    // A cleanly received frame rejected by the selected parser may belong to
    // another remote. Restore the library only; do not arm a hardware reset.
    ESP_LOGD(TAG, "RX filtered: selected parser rejected frame; total=%lu streak=%lu; restoring candidate=%u",
             (unsigned long) rx_parse_failures_, (unsigned long) rx_parse_failure_streak_,
             unsigned(candidate_.match) + 1);
    // Roll back immediately, not only before the next HA transmit. Reinitialize
    // from immutable pairing samples and replay the same candidate/extra sample.
    // The rejected frame is not retried (parse may have mutated REV buffers).
    // No IR is sent, no preferences are written, and no candidate is advanced.
    if (!apply_pairing_(candidate_)) {
      fault_("解析失敗且恢復原配對失敗；請重新檢查通訊");
      return; // Do not let listen_() hide this fault.
    }
    ESP_LOGI(TAG, "RX recovery: selected pairing restored; waiting for next frame");
    publish_settings_();
    status_message_("已過濾目前配對無法解析的訊號；保留最後有效狀態");
  }
  listen_(); // Do not retransmit received IR.
}

void BC7215AClimate::control(const climate::ClimateCall &call) {
  ESP_LOGI(TAG, "CONTROL request: mode=%d temp=%.1f fan=%d phase=%u",
           call.get_mode().has_value() ? int(*call.get_mode()) : -1,
           call.get_target_temperature().value_or(-1.0f),
           call.get_fan_mode().has_value() ? int(*call.get_fan_mode()) : -1,
           unsigned(phase_));
  if (!call.get_mode().has_value() && !call.get_target_temperature().has_value() &&
      !call.get_fan_mode().has_value()) return;
  if (!paired_ok_ || phase_ == FAULT) { status_message_("請先完成配對"); return; }
  // Native API/Web requests may take precedence over a queued Matter batch.
  matter_command_debounce_ = false;
  if (!pending_) {
    desired_ = (phase_ == LISTEN) ? current_ : active_;
    explicit_off_ = false;
  }
  if (call.get_target_temperature().has_value()) {
    float t=*call.get_target_temperature();
    if (!std::isfinite(t) || t<16 || t>30) return;
    key_ = t>=desired_.temperature ? KEY_PLUS : KEY_MINUS;
    desired_.temperature=lroundf(t);
  }
  if (call.get_mode().has_value()) {
    auto m=*call.get_mode();
    explicit_off_ = m == climate::CLIMATE_MODE_OFF;
    if (m == climate::CLIMATE_MODE_OFF) desired_.power=false;
    else {
      int found=-1;
      for (int i=0;i<5;i++) if (m==MODES[i]) found=i;
      if (found<0) return;
      desired_.mode=found; desired_.power=true;
    }
    key_=KEY_MODE;
  }
  if (call.get_fan_mode().has_value()) {
    int found=-1;
    for (int i=0;i<4;i++) if (*call.get_fan_mode()==FANS[i]) found=i;
    if (found<0) return;
    desired_.fan=found; key_=KEY_FAN;
  }
  // Changing setpoint while off does not turn the AC on.
  if (phase_==LISTEN && !current_.power && !desired_.power && !call.get_mode().has_value()) {
    current_=desired_; publish_settings_(); return;
  }
  pending_=true; // Coalesce changes while a previous IR command is completing.
}


void BC7215AClimate::matter_set_system_mode(uint8_t value) {
  climate::ClimateMode target;
  switch (value) {
    case 0: target = climate::CLIMATE_MODE_OFF; break;
    case 1: target = climate::CLIMATE_MODE_AUTO; break;
    case 3: target = climate::CLIMATE_MODE_COOL; break;
    case 4: target = climate::CLIMATE_MODE_HEAT; break;
    case 7: target = climate::CLIMATE_MODE_FAN_ONLY; break;
    case 8: target = climate::CLIMATE_MODE_DRY; break;
    default: return;
  }
  auto call = this->make_call();
  call.set_mode(target);
  call.perform();
  if (pending_) { matter_command_debounce_ = true; matter_command_since_ = millis(); }
}

void BC7215AClimate::matter_set_power(bool on) {
  auto call = this->make_call();
  const auto &settings = pending_ ? desired_ : current_;
  call.set_mode(on ? MODES[settings.mode] : climate::CLIMATE_MODE_OFF);
  call.perform();
  if (pending_) { matter_command_debounce_ = true; matter_command_since_ = millis(); }
}

void BC7215AClimate::matter_set_target_temperature(int16_t centi) {
  const float previous = pending_ ? desired_.temperature : current_.temperature;
  const float raw = centi / 100.0f;
  const float target = std::clamp(raw > previous ? std::ceil(raw) : std::floor(raw), 16.0f, 30.0f);
  auto call = this->make_call();
  call.set_target_temperature(target);
  call.perform();
  if (pending_) { matter_command_debounce_ = true; matter_command_since_ = millis(); }
}

void BC7215AClimate::matter_set_fan_mode(uint8_t value) {
  // The FanControl server normalizes percent/speed writes to this enum.
  if (value == 0) { matter_set_power(false); return; }
  int fan = value >= 1 && value <= 3 ? value : value == 4 ? 3 :
            (value == 5 || value == 6) ? 0 : -1;
  if (fan < 0) return;
  auto call = this->make_call();
  call.set_fan_mode(FANS[fan]);
  call.perform();
  if (pending_) { matter_command_debounce_ = true; matter_command_since_ = millis(); }
}
uint8_t BC7215AClimate::matter_fan_mode() const {
  if (!current_.power) return 0;
  return current_.fan == 0 ? 5 : current_.fan;
}

uint8_t BC7215AClimate::matter_system_mode() const {
  switch (this->mode) {
    case climate::CLIMATE_MODE_OFF: return 0;
    case climate::CLIMATE_MODE_AUTO: return 1;
    case climate::CLIMATE_MODE_COOL: return 3;
    case climate::CLIMATE_MODE_HEAT: return 4;
    case climate::CLIMATE_MODE_FAN_ONLY: return 7;
    case climate::CLIMATE_MODE_DRY: return 8;
    default: return 0;
  }
}
uint8_t BC7215AClimate::matter_running_mode() const {
  // Inferred from the last confirmed IR state; not compressor telemetry.
  if (!known_ || !current_.power) return 0;
  return current_.mode == 1 ? 3 : current_.mode == 2 ? 4 : 0;
}
uint16_t BC7215AClimate::matter_running_state() const {
  if (!known_ || !current_.power) return 0;
  if (current_.mode == 1) return 2; // Cool
  if (current_.mode == 2) return 1; // Heat
  return current_.mode == 4 ? 4 : 0; // Fan only; don't guess Auto/Dry compressor state.
}
int16_t BC7215AClimate::matter_target_temperature_centi() const {
  if (!std::isfinite(this->target_temperature)) return 2500;
  return static_cast<int16_t>(std::lround(std::clamp(this->target_temperature, 16.0f, 30.0f) * 100.0f));
}

void BC7215AClimate::begin_command_() {
  matter_command_debounce_ = false;
  if (!current_.power && !desired_.power && !explicit_off_) {
    current_=desired_; pending_=false; publish_settings_(); return;
  }
  explicit_off_=false;
  active_=desired_; active_key_=key_; pending_=false;
  ESP_LOGI(TAG, "IR command begin: power=%d mode=%d temp=%d fan=%d known=%d",
           active_.power, active_.mode, active_.temperature, active_.fan, known_);
  ac_->stopCapture();
  // Cancel an incomplete module command before loading a new format. Reset is
  // non-IR; the datasheet requires >=36 ms before another UART command.
  driver_->abortCommand();
  delay(40);
  stream_->drain_raw();
  driver_->resetReceiveState();
  ESP_LOGD(TAG, "TX prepare: abort=7A, settle=40ms; stale UART discarded before F6/F5");
  if (base_needs_restore_ || !current_.power || !known_) {
    if (!apply_pairing_(candidate_)) { fault_("恢復基礎封包失敗；請重新配對"); return; }
    base_needs_restore_ = false;
  }
  const bool need_on = !current_.power || !known_;
  const bc7215DataVarPkt_t *result=nullptr;
  if (!active_.power) { phase_=TX_OFF; result=ac_->off(); }
  else if (need_on) { phase_=TX_ON; result=ac_->on(); }
  else { send_settings_(); return; }
  since_=millis();
  if (!result) fault_("空調程式庫未產生指令，請重啟並重新配對");
}
void BC7215AClimate::send_settings_() {
  phase_=TX_SETTINGS;
  auto result=ac_->setTo(active_.temperature,active_.mode,active_.fan,active_key_);
  since_=millis();
  if (!result) fault_("空調程式庫未產生設定指令，請重啟並重新配對");
}
void BC7215AClimate::finish_command_() {
  current_=active_; known_=true;
  publish_settings_();
  listen_();
  status_message_("紅外線已發送；狀態依指令更新");
}
void BC7215AClimate::loop() {
  if (!driver_) return;
  stream_->log_trace();
  if (phase_ == FAULT) {
    stream_->drain_raw();
    stream_->log_trace();
    if (millis() - diagnostic_since_ >= 5000) {
      diagnostic_since_ = millis();
      log_diagnostics_();
    }
    return;
  }
  if (phase_ == UNPAIRED) {
    stream_->drain_raw();
    return;
  }
  if (driver_->io_error || stream_->io_error) {
    if (phase_ == TX_ON || phase_ == TX_SETTINGS || phase_ == TX_OFF)
      recover_failed_tx_("紅外傳送異常，這次指令未確認完成");
    else fault_("BUSY或UART通訊異常；已恢復接收，可重試通訊");
    return;
  }
  if (service_rx_recovery_()) return;
  if (phase_ == PROBE_WAKE || phase_ == PROBE_TX_WAIT) {
    stream_->drain_raw(); // Log pre-probe bytes, but do not accept them as the ACK.
    if (millis() - since_ < 50) return;
    if (phase_ == PROBE_WAKE) {
      driver_->setTx();
      phase_ = PROBE_TX_WAIT;
      since_ = millis();
    } else {
      if (stream_->available()) {
        if (millis() - since_ > 1500) fault_("探測前RX持續有資料；未送探測，請停止遙控器發射後重試");
        return;
      }
      probe_rx_start_ = stream_->rx_count;
      probe_tx_start_ = stream_->tx_count;
      if (digitalRead(mod_->get_pin()) != LOW) {
        fault_("探測時MOD讀回不是低電位；未送探測，請查看GPIO狀態");
        return;
      }
      driver_->setShutDown();
      phase_ = PROBE;
      since_ = millis();
      ESP_LOGI(TAG, "PROBE F7 00: writes=%lu, write-start delta=%lu us (software timing)",
               (unsigned long) (stream_->tx_count - probe_tx_start_),
               (unsigned long) stream_->write_delta_us);
      log_diagnostics_();
    }
    return;
  }
  if (phase_==PROBE) {
    if (driver_->cmdCompleted()) {
      ESP_LOGI(TAG, "PROBE received 7A; RX bytes=%lu",
               (unsigned long) (stream_->rx_count - probe_rx_start_));
      driver_->setRx(); delay(50); driver_->setTx();
      if (apply_pairing_(saved_)) {
        candidate_=saved_; paired_ok_=true;
        if (!count_candidates_()) return;
        publish_match_();
        if (paired_) paired_->publish_state(true);
        listen_(); status_message_("配對已載入；狀態為上次記錄，等待同步");
      } else {
        // A missing/invalid saved pairing is idle, not a user pairing request.
        // Keep network LED status visible until Start Pairing is pressed.
        ac_->stopCapture();
        candidate_ = PairingData{};
        paired_ok_ = known_ = pending_ = false;
        phase_ = UNPAIRED;
        driver_->setRx();
        if (paired_) paired_->publish_state(false);
        if (known_sensor_) known_sensor_->publish_state(false);
        if (match_info_) match_info_->publish_state("尚未配對");
        if (pairing_id_) pairing_id_->publish_state("尚未配對");
        status_message_("通訊正常，尚未配對；請按開始配對");
      }
    } else if (millis()-since_>1500) {
      const auto received = stream_->rx_count - probe_rx_start_;
      fault_(received == 0 ? "探測1500ms內未收到UART資料；已恢復接收，可重試通訊" :
                            "探測收到UART資料但未確認7A；請查看RX紀錄並重試通訊");
    }
    return;
  }
  if (phase_==TX_ON || phase_==TX_SETTINGS || phase_==TX_OFF) {
    // Read the ACK before testing the deadline, including a late loop iteration.
    if (!ac_->isBusy()) {
      ESP_LOGD(TAG, "TX completed: phase=%u elapsed=%lu ms", unsigned(phase_), (unsigned long)(millis()-since_));
      if (phase_==TX_ON) {
        delay(45); // Required inter-frame gap starts AFTER the completion ACK.
        send_settings_();
      } else finish_command_();
    } else if (millis()-since_>3000) {
      recover_failed_tx_("紅外發送完成回覆逾時");
    }
    return;
  }
  ac_->setCaptureProfile(phase_ == LISTEN && paired_ok_ ? candidate_.base.count : 0,
                         candidate_.base.data, candidate_.base.status);
  if (ac_->signalCaptured()) {
    if (phase_ != LISTEN) ac_->stopCapture();
    if (ac_->capture_overflow) {
      // Drop this incomplete capture without replacing the user's HA status.
      // Keep the buffer bound: truncated multi-segment frames must not be parsed.
      ESP_LOGD(TAG, "RX capture overflow: discarded; resuming reception");
      if (phase_ == LISTEN) ac_->resumeCapture();
      else ac_->startCapture();
    } else received_();
  } else if (phase_==LISTEN && pending_ &&
             (!matter_command_debounce_ || millis() - matter_command_since_ >= 700) &&
             !ac_->isBusy() && ac_->sampleCount==0) {
    begin_command_();
  }
  service_rx_watch_();
}
void BC7215AClimate::dump_config() {
  ESP_LOGCONFIG(TAG, "BC7215A ESPHome v1.0.33 Matter; library %s", ac_ ? ac_->getLibVer() : "?");
  LOG_PIN("  MOD: ", mod_); LOG_PIN("  BUSY: ", busy_);
  this->check_uart_settings(19200, 2, uart::UART_CONFIG_PARITY_NONE, 8);
}
}  // namespace esphome::bc7215a
