#include "bc7215a_climate.h"
#include "pairing_fingerprint.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace esphome::bc7215a {
static constexpr uint32_t SWING_MAGIC = 0xBC720106;

bool BC7215AClimate::is_learning_swing() const {
  return phase_ == LEARN_WAIT || phase_ == LEARN_CAPTURE;
}

bool BC7215AClimate::swing_available_(int mode) const {
  if (mode < 0 || mode > 2 || !paired_ok_ || swings_.magic != SWING_MAGIC ||
      swings_.pairing != pairing_fingerprint(candidate_)) return false;
  return mode == 0 ? (swings_.axes[0].ready || swings_.axes[1].ready) : swings_.axes[mode - 1].ready;
}

void BC7215AClimate::publish_swing_() {
  text_sensor::TextSensor *sensors[] = {vertical_swing_status_, horizontal_swing_status_};
  for (unsigned i = 0; i < 2; i++) {
    if (!sensors[i]) continue;
    const char *text = !paired_ok_ ? "等待配對／通訊" :
                       swing_available_(i + 1) ? "已學習，待實機確認效果" : "未學習／資料不適用";
    if (is_learning_swing() && learning_direction_ == i)
      text = learning_step_ == 0 ? "學習中：擺風關" : "學習中：擺風開";
    sensors[i]->publish_state(text);
  }
  if (swing_state_) {
    const char *text = "未知；樣本未能唯一辨識，選單保留上次選項";
    if (!paired_ok_) text = "等待配對／通訊";
    else if (is_learning_swing()) text = "學習中，尚未確認擺風狀態";
    else if (current_.swing >= 0) {
      if (!current_.power) text = "關機中；選項暫存，開機時套用";
      else if (current_.swing_from_remote)
        text = current_.swing == 1 ? "依遙控樣本辨識：上下開、左右關" :
               current_.swing == 2 ? "依遙控樣本辨識：上下關、左右開" : "依遙控樣本辨識：上下關、左右關";
      else text = current_.swing == 1 ? "依指令：上下開、左右關；無實機回報" :
                  current_.swing == 2 ? "依指令：上下關、左右開；無實機回報" :
                  "依指令：上下關、左右關；無實機回報";
    }
    swing_state_->publish_state(text);
  }
}

void BC7215AClimate::load_swing_() {
  if (swings_.magic != SWING_MAGIC || swings_.pairing != pairing_fingerprint(candidate_)) {
    swings_ = SwingData{};
    swings_.magic = SWING_MAGIC;
    swings_.pairing = pairing_fingerprint(candidate_);
  }
  // Loading preferences never generates IR or assumes the AC's physical state.
  current_.swing = desired_.swing = -1;
  base_needs_restore_ = false;
  publish_swing_();
}

void BC7215AClimate::swing_prompt_() {
  char message[240];
  const char *direction = learning_direction_ ? "左右" : "上下";
  const char *other = learning_direction_ ? "上下" : "左右";
  if (phase_ == LEARN_WAIT)
    snprintf(message, sizeof(message), "%s學習：維持開機、相同溫度/模式/風速，%s關閉；準備好後按擷取樣本，再送出%s%s",
             direction, other, direction, learning_step_ ? "開" : "關");
  else
    snprintf(message, sizeof(message), "等待完整訊號：%s%s、%s關閉；其他設定勿改；每次只按一下",
             direction, learning_step_ ? "開" : "關", other);
  status_message_(message);
  publish_swing_();
}

void BC7215AClimate::start_swing_learning(bool horizontal) {
  if (!ac_ || phase_ != LISTEN || !paired_ok_ || pending_ || ac_->isBusy() || ac_->sampleCount != 0) {
    status_message_("請先完成配對，等待收發結束後再學習擺風"); return;
  }
  if (saved_.magic != candidate_.magic || pairing_fingerprint(saved_) != pairing_fingerprint(candidate_)) {
    status_message_("目前配對尚未保存，請先重新配對並確認保存成功"); return;
  }
  ac_->stopCapture();
  if (!apply_pairing_(candidate_)) { fault_("擺風學習前恢復配對失敗"); return; }
  learning_direction_ = horizontal ? 1 : 0;
  learning_step_ = 0;
  learning_axis_ = SwingAxis{};
  learning_since_ = millis();
  phase_ = LEARN_WAIT;
  current_.swing = desired_.swing = -1;
  known_ = false;
  publish_settings_();
  ac_->startCapture();
  swing_prompt_();
}

void BC7215AClimate::arm_swing_sample() {
  if (phase_ != LEARN_WAIT) return;
  if (ac_->isBusy() || ac_->sampleCount != 0) {
    status_message_("仍在接收準備訊號，請放開遙控器，稍後再按擷取樣本"); return;
  }
  ac_->stopCapture();
  phase_ = LEARN_CAPTURE;
  learning_since_ = millis();
  ac_->startCapture();
  swing_prompt_();
}

void BC7215AClimate::cancel_swing_learning() {
  if (!is_learning_swing()) return;
  ac_->stopCapture();
  if (!apply_pairing_(candidate_)) { fault_("取消擺風學習後恢復配對失敗"); return; }
  learning_axis_ = SwingAxis{};
  known_ = false;
  current_.swing = -1;
  desired_ = current_;
  pending_ = false;
  base_needs_restore_ = false;
  listen_();
  publish_settings_();
  status_message_("擺風學習已取消，保留原有樣本；請用遙控器同步");
}

bool BC7215AClimate::replace_swing_sample_(const SwingSample &sample) {
  // This bundled library copies the byte count through uint8_t. Reject >=256
  // bytes rather than silently truncating a large packet. Stored packets have
  // already had REV normalized and multi-segment data flattened by the library.
  if (sample.data.bitLen == 0 || sample.data.bitLen > 255 * 8 || (sample.status & 0xC0)) return false;
  if (!bc7215_ac_replace_base(sample.status, reinterpret_cast<const bc7215DataVarPkt_t *>(&sample.data)))
    return false;
  int8_t t = -1, m = -1, f = -1, p = -1;
  return bc7215_ac_parse(&t, &m, &f, &p) && t >= 0 && t <= 14 && m >= 0 && m < 5 &&
         f >= 0 && f < 4 && p == 1;
}

void BC7215AClimate::learn_swing_received_() {
  capture_(learning_capture_); // Preserve raw data before vendor parse normalizes REV.
  bool ok = valid_(learning_capture_) && learning_capture_.count <= 3;
  // The actual bundled replace_base rejects a count of four; do not silently
  // lose segments. Require the paired packet shape, not a short toggle-only code.
  ok = ok && learning_capture_.count == candidate_.base.count;
  unsigned total_bits = 0;
  for (unsigned i = 0; ok && i < learning_capture_.count; i++) {
    total_bits += learning_capture_.data[i].bitLen;
    ok = learning_capture_.data[i].bitLen <= 255 * 8 &&
         learning_capture_.data[i].bitLen == candidate_.base.data[i].bitLen &&
         (learning_capture_.status[i] & 0x3F) == (candidate_.base.status[i] & 0x3F);
  }
  ok = ok && total_bits <= 255 * 8;
  if (!apply_pairing_(candidate_)) { fault_("擺風取樣前恢復配對失敗"); return; }
  int t = -1, m = -1, f = -1, p = -1;
  if (ok) {
    apply_capture_(learning_capture_);
    ok = ac_->parse(t, m, f, p) && t >= 16 && t <= 30 && m >= 0 && m < 5 &&
         f >= 0 && f < 4 && p == 1;
  }
  if (ok && learning_step_)
    ok = t == learning_settings_.temperature && m == learning_settings_.mode && f == learning_settings_.fan;
  SwingSample &sample = learning_step_ ? learning_axis_.on : learning_axis_.off;
  if (ok) {
    const auto *base = bc7215_ac_get_base_data();
    ok = base && base->bitLen > 0 && base->bitLen <= 255 * 8;
    if (ok) {
      sample = SwingSample{};
      sample.data.bitLen = base->bitLen;
      const size_t size = (base->bitLen + 7) / 8;
      memcpy(sample.data.data, base->data, size);
      // Single-segment get_base_data retains raw REV; combined packets are
      // already normalized by BC7215AC::parse and the library's flattener.
      sample.status = learning_capture_.count == 1 ? learning_capture_.status[0] & 0x3F :
                      bc7215_ac_get_base_fmt()->signature.inByte & 0x3F;
      if (learning_capture_.count == 1 && (learning_capture_.status[0] & 0x40))
        for (size_t i = 0; i < size; i++) sample.data.data[i] = ~sample.data.data[i];
      // Verify the compact persisted representation can round-trip through the
      // selected library candidate before accepting it.
      ok = replace_swing_sample_(sample);
      if (ok) {
        int8_t rt = -1, rm = -1, rf = -1, rp = -1;
        ok = bc7215_ac_parse(&rt, &rm, &rf, &rp) && rt + 16 == t && rm == m && rf == f && rp == p;
      }
      if (ok && learning_step_) {
        const auto &off = learning_axis_.off;
        ok = sample.status == off.status && sample.data.bitLen == off.data.bitLen &&
             memcmp(sample.data.data, off.data.data, size) != 0;
      }
    }
  }
  // Restore even on failure: replace_base can mutate library state before failing.
  if (!apply_pairing_(candidate_)) { fault_("擺風取樣後恢復配對失敗"); return; }
  phase_ = LEARN_WAIT;
  learning_since_ = millis();
  if (!ok) {
    ac_->startCapture();
    status_message_("樣本不適用：需相同設定、開機完整碼且開關資料不同；不支援4段或過長碼。準備好後重新擷取");
    publish_swing_();
    return;
  }
  current_.temperature = t; current_.mode = m; current_.fan = f; current_.power = true;
  current_.swing = -1; known_ = true; desired_ = current_;
  if (!learning_step_) {
    learning_settings_ = current_;
    learning_step_ = 1;
    ac_->startCapture();
    publish_settings_();
    swing_prompt_();
    return;
  }
  learning_axis_.ready = true;
  // Commit only a complete pair. The other direction and the previous pair
  // remain usable if capture is cancelled or rejected.
  std::swap(swings_.axes[learning_direction_], learning_axis_);
  if (!swing_pref_.save(&swings_) || !global_preferences->sync()) {
    std::swap(swings_.axes[learning_direction_], learning_axis_);
    // Replace any pending preference-cache write with the previous valid data.
    swing_pref_.save(&swings_);
    global_preferences->sync();
    ac_->startCapture();
    publish_settings_();
    status_message_("擺風保存失敗；保留舊樣本，請取消後重試；未確認寫入快閃記憶體");
    return;
  }
  // Keep the accepted ON frame as the live base, so a normal temperature
  // command immediately after learning does not undo the sampled swing state.
  if (!replace_swing_sample_(swings_.axes[learning_direction_].on)) {
    fault_("擺風樣本已保存，但載入失敗；請重啟後重新學習"); return;
  }
  learning_axis_ = SwingAxis{};
  base_needs_restore_ = false;
  listen_();
  publish_settings_();
  status_message_("擺風開關樣本已保存；請從空調選單測試。收到不同資料不代表已證實冷氣支援");
}

bool BC7215AClimate::prepare_swing_() {
  if (active_.swing < 0 || active_.swing_from_remote) return true; // Retain the remote's latest base packet.
  if (!swing_available_(active_.swing)) {
    pending_ = false;
    listen_();
    status_message_("擺風樣本不適用；未發送設定指令，請重新學習");
    return false;
  }
  unsigned axis = active_.swing ? active_.swing - 1 :
                  current_.swing == 2 && swings_.axes[1].ready ? 1 : swings_.axes[0].ready ? 0 : 1;
  const auto &profile = swings_.axes[axis];
  if (replace_swing_sample_(active_.swing ? profile.on : profile.off)) return true;
  swings_.axes[axis].ready = false;
  pending_ = false;
  current_.swing = desired_.swing = -1;
  if (!apply_pairing_(candidate_)) { fault_("擺風樣本失效且恢復配對失敗"); return false; }
  listen_();
  publish_settings_();
  status_message_("擺風樣本失效；已恢復配對基礎封包，請重新學習");
  return false;
}
}  // namespace esphome::bc7215a
