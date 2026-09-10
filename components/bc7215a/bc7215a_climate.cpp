#include "bc7215a_climate.h"
#include "pairing_fingerprint.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstring>
#include <algorithm>

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
  bus_->write_byte(c);
  tx_count++;
  if (tx_size_ < sizeof(tx_trace_)) tx_trace_[tx_size_++] = c;
  else tx_dropped_++;
  return 1;
}
void UARTStream::flush() {
  if (io_error) return;
  io_error = bus_->flush() != uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS;
}
void UARTStream::drain_raw() {
  // Fault mode remains observable without feeding unpaired frames to the AC library.
  for (unsigned i = 0; i < 256 && available(); i++) if (read() < 0) break;
}
void UARTStream::log_trace() {
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
  t.set_supported_swing_modes({climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_HORIZONTAL});
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
  publish_swing_();
  if (paired_) paired_->publish_state(false);
  if (known_sensor_) known_sensor_->publish_state(false);
  // F7 may have succeeded even when its ACK was lost. Wake the module for raw RX.
  if (driver_) driver_->setRx();
  this->status_set_error(LOG_STR("BC7215A communication fault"));
  status_message_(message);
  log_diagnostics_();
}
void BC7215AClimate::log_diagnostics_() {
  if (!stream_) return;
  ESP_LOGI(TAG, "DIAG phase=%u MOD=%d BUSY=%d TX=%lu RX=%lu busy_error=%d uart_error=%d",
           unsigned(phase_), digitalRead(mod_->get_pin()), digitalRead(busy_->get_pin()),
           (unsigned long) stream_->tx_count, (unsigned long) stream_->rx_count,
           driver_->io_error, stream_->io_error);
  stream_->log_trace();
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
  publish_swing_();
  since_ = millis();
  this->status_clear_error();
  status_message_("檢查 BC7215A 通訊：先喚醒模組");
}
bool BC7215AClimate::is_pairing() const {
  return phase_ == PAIR || phase_ == EXTRA || is_learning_swing();
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
  ESP_LOGI(TAG, "RX recovery start: attempt=%u, automatic guard retained, abort=7A, drained=%u; no IR sent",
           rx_recovery_attempts_, drained);
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
  if (ack) ESP_LOGI(TAG, "RX recovery UART ACK=7A; waking receiver (IR recovery unverified)");
  else ESP_LOGW(TAG, "RX recovery UART ACK timeout; waking receiver, automatic guard retained");
  driver_->resetReceiveState();
  ac_->startCapture(); // MOD high wakes the module, then restores composite mode.
  rx_recovery_stage_ = 0;
  rx_watch_since_ = millis();
  status_message_(ack ? "模組已回覆；等待有效遙控訊號確認恢復" :
                       "模組未確認回覆；已嘗試喚醒，等待有效遙控訊號");
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
  if (phase_ != LISTEN) return; // Pause through TX and swing learning, retain protection.
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
  ac_->setCelsius();
  if (version_) version_->publish_state(ac_->getLibVer());
  if (match_info_) match_info_->publish_state("尚未配對");
  if (pairing_id_) pairing_id_->publish_state("尚未配對");
  pref_ = global_preferences->make_preference<PairingData>(MAGIC);
  pref_.load(&saved_);
  swing_pref_ = global_preferences->make_preference<SwingData>(0xBC720106);
  swing_pref_.load(&swings_);
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
  if (is_learning_swing()) { status_message_("請先完成或取消擺風學習"); return; }
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
  publish_swing_();
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
  load_swing_();
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
    load_swing_();
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
  PairingData old = candidate_;
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
    if (!apply_pairing_(old)) { start_pairing(); return; }
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
  if (current_.swing >= 0)
    swing_mode = current_.swing == 1 ? climate::CLIMATE_SWING_VERTICAL :
                 current_.swing == 2 ? climate::CLIMATE_SWING_HORIZONTAL : climate::CLIMATE_SWING_OFF;
  publish_swing_();
  if (known_sensor_) known_sensor_->publish_state(known_);
  this->publish_state();
}
void BC7215AClimate::received_() {
  if (is_learning_swing()) {
    if (phase_ == LEARN_CAPTURE) learn_swing_received_();
    else ac_->startCapture(); // Preparation frames are deliberately ignored.
    return;
  }
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
    sync_remote_swing_();
    if (phase_ == FAULT) return;
    if (pending_ && (desired_.swing < 0 || desired_.swing_from_remote)) {
      desired_.swing = current_.swing;
      desired_.swing_from_remote = current_.swing_from_remote;
    }
    if (!pending_) desired_=current_;
    publish_settings_();
    status_message_(p==2 && !known_ ? "收到切換電源碼，但起始電源狀態未知" : "收到原廠遙控器訊號，已更新HA");
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
  if (is_learning_swing()) { status_message_("擺風學習中；請完成或取消後再控制冷氣"); return; }
  if (call.get_swing_mode().has_value()) {
    const auto requested = *call.get_swing_mode();
    const int value = requested == climate::CLIMATE_SWING_OFF ? 0 :
                      requested == climate::CLIMATE_SWING_VERTICAL ? 1 :
                      requested == climate::CLIMATE_SWING_HORIZONTAL ? 2 : -1;
    if (!swing_available_(value)) {
      status_message_("此方向尚未完成擺風學習；未發送指令"); return;
    }
  }
  if (!call.get_mode().has_value() && !call.get_target_temperature().has_value() &&
      !call.get_fan_mode().has_value() && !call.get_swing_mode().has_value()) return;
  if (!paired_ok_ || phase_ == FAULT) { status_message_("請先完成配對"); return; }
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
  if (call.get_swing_mode().has_value()) {
    desired_.swing_from_remote = false;
    const auto requested = *call.get_swing_mode();
    desired_.swing = requested == climate::CLIMATE_SWING_VERTICAL ? 1 :
                     requested == climate::CLIMATE_SWING_HORIZONTAL ? 2 : 0;
    // No native swing key exists. The library regenerates the four normal fields.
    key_ = KEY_FAN;
  }
  // Changing setpoint while off does not turn the AC on.
  if (phase_==LISTEN && !current_.power && !desired_.power && !call.get_mode().has_value()) {
    current_=desired_; publish_settings_(); return;
  }
  pending_=true; // Coalesce changes while a previous IR command is completing.
}
void BC7215AClimate::begin_command_() {
  if (!current_.power && !desired_.power && !explicit_off_) {
    current_=desired_; pending_=false; publish_settings_(); return;
  }
  explicit_off_=false;
  active_=desired_; active_key_=key_; pending_=false;
  ac_->stopCapture();
  if (base_needs_restore_ || !current_.power || !known_) {
    if (!apply_pairing_(candidate_)) { fault_("恢復基礎封包失敗；請重新配對"); return; }
    // The received live base is gone; retain a recognized selection by using
    // its learned template for the ensuing command instead.
    active_.swing_from_remote = false;
    base_needs_restore_ = false;
  }
  if (active_.power && active_.swing >= 0 && (!current_.power || !known_)) {
    // Validate before even the preliminary ON transmission. Keep the normal ON
    // sequence on the paired base; send_settings_ installs the learned sample.
    if (!prepare_swing_()) return;
    if (!apply_pairing_(candidate_)) { fault_("擺風預檢後恢復配對失敗"); return; }
  }
  const bc7215DataVarPkt_t *result=nullptr;
  if (!active_.power) { phase_=TX_OFF; result=ac_->off(); }
  else if (!current_.power || !known_) { phase_=TX_ON; result=ac_->on(); }
  else { send_settings_(); return; }
  since_=millis();
  if (!result) fault_("空調程式庫未產生指令，請重啟並重新配對");
}
void BC7215AClimate::send_settings_() {
  if (!prepare_swing_()) return;
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
  if (driver_->io_error) { fault_("BUSY持續高逾時；已恢復接收，可按重新檢查通訊"); return; }
  if (stream_->io_error) { fault_("UART傳送完成檢查失敗；已恢復接收，可重試通訊"); return; }
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
        load_swing_();
        if (paired_) paired_->publish_state(true);
        listen_(); status_message_("配對已載入；狀態為上次記錄，等待同步");
      } else { phase_=PAIR; start_pairing(); }
    } else if (millis()-since_>1500) {
      const auto received = stream_->rx_count - probe_rx_start_;
      fault_(received == 0 ? "探測1500ms內未收到UART資料；已恢復接收，可重試通訊" :
                            "探測收到UART資料但未確認7A；請查看RX紀錄並重試通訊");
    }
    return;
  }
  if (phase_==TX_ON || phase_==TX_SETTINGS || phase_==TX_OFF) {
    if (millis()-since_>3000) { fault_("紅外線發送逾時，請檢查模組後重啟"); return; }
    if (millis()-since_>=200 && !ac_->isBusy()) {
      if (phase_==TX_ON) send_settings_();
      else finish_command_();
    }
    return;
  }
  if (is_learning_swing() && millis() - learning_since_ >= 90000) {
    cancel_swing_learning();
    if (phase_ != FAULT) status_message_("擺風學習逾時，保留原有樣本；請重新開始");
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
  } else if (phase_==LISTEN && pending_ && !ac_->isBusy() && ac_->sampleCount==0) {
    begin_command_();
  }
  service_rx_watch_();
}
void BC7215AClimate::dump_config() {
  ESP_LOGCONFIG(TAG, "BC7215A ESPHome v1.0.19; library %s", ac_ ? ac_->getLibVer() : "?");
  LOG_PIN("  MOD: ", mod_); LOG_PIN("  BUSY: ", busy_);
  this->check_uart_settings(19200, 2, uart::UART_CONFIG_PARITY_NONE, 8);
}
}  // namespace esphome::bc7215a
