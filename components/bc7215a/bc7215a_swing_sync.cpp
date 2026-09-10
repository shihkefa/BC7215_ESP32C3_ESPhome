#include "bc7215a_climate.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome::bc7215a {

bool BC7215AClimate::canonical_swing_(const SwingSample &sample, SwingCanonical &out) {
  // Every attempt starts with the selected candidate. Some library paths switch
  // protocol/format internally, including on failure. Do not inherit that state.
  if (!apply_pairing_(candidate_) || !replace_swing_sample_(sample)) return false;
  // Generate in memory only: BC7215AC::setTo would transmit, and must not be used.
  // The same normal settings and key remove their differences AND recalculate
  // checksums; no guessed bit masks or brand-specific offsets are involved.
  const auto *data = bc7215_ac_set(9, 1, 0, KEY_FAN);
  if (!data) return false;
  const auto *format = bc7215_ac_get_base_fmt();
  if (!data->bitLen) {
    const auto *message = reinterpret_cast<const bc7215CombinedMsg_t *>(data);
    format = message->body.msg.fmt;
    data = message->body.msg.datPkt;
  }
  if (!format || !data || !data->bitLen || data->bitLen > 255 * 8) return false;
  out = SwingCanonical{};
  out.format = *format;
  out.data.bitLen = data->bitLen;
  memcpy(out.data.data, data->data, (data->bitLen + 7) / 8);
  return true;
}

bool BC7215AClimate::same_swing_(const SwingCanonical &a, const SwingCanonical &b) const {
  return a.data.bitLen == b.data.bitLen && a.format.signature.inByte == b.format.signature.inByte &&
         memcmp(a.format.format, b.format.format, sizeof(a.format.format)) == 0 &&
         memcmp(a.data.data, b.data.data, (a.data.bitLen + 7) / 8) == 0;
}

void BC7215AClimate::sync_remote_swing_() {
  current_.swing = -1;
  current_.swing_from_remote = false;
  base_needs_restore_ = !current_.power;
  if (!swing_available_(0) || !current_.power || ac_->sampleCount == 0 || ac_->sampleCount > 3 ||
      ac_->sampleCount != candidate_.base.count) return;

  // parse() has already installed the received packet. Save it before any
  // comparisons so the next normal command keeps the remote's arbitrary bits.
  unsigned total_bits = 0;
  for (unsigned i = 0; i < ac_->sampleCount; i++) {
    total_bits += ac_->sampleData[i].bitLen;
    if (ac_->sampleData[i].bitLen != candidate_.base.data[i].bitLen ||
        (ac_->sampleStatus[i] & 0x3F) != (candidate_.base.status[i] & 0x3F)) return;
  }
  const auto *base = bc7215_ac_get_base_data();
  if (!base || !base->bitLen || total_bits > 255 * 8 || base->bitLen > 255 * 8) return;
  int8_t t = -1, m = -1, f = -1, p = -1;
  if (!bc7215_ac_parse(&t, &m, &f, &p) || p != 1) return;
  received_swing_ = SwingSample{};
  received_swing_.data.bitLen = base->bitLen;
  memcpy(received_swing_.data.data, base->data, (base->bitLen + 7) / 8);
  received_swing_.status = ac_->sampleCount == 1 ? ac_->sampleStatus[0] & 0x3F :
                            bc7215_ac_get_base_fmt()->signature.inByte & 0x3F;
  if (ac_->sampleCount == 1 && (ac_->sampleStatus[0] & 0x40))
    for (unsigned i = 0; i < (base->bitLen + 7) / 8; i++)
      received_swing_.data.data[i] = ~received_swing_.data.data[i];

  unsigned matches = 0;
  bool reliable = canonical_swing_(received_swing_, canonical_rx_);
  for (unsigned axis = 0; reliable && axis < 2; axis++) {
    if (!swing_available_(axis + 1)) continue;
    const auto &profile = swings_.axes[axis];
    // A generator may erase the very feature we are trying to identify. If the
    // normalized ON and OFF are identical, or a learned axis cannot be compared,
    // leave the whole state unknown instead of selecting a different axis.
    reliable = canonical_swing_(profile.off, canonical_off_) &&
               canonical_swing_(profile.on, canonical_on_) && !same_swing_(canonical_off_, canonical_on_);
    if (!reliable) break;
    if (same_swing_(canonical_rx_, canonical_off_)) matches |= 1U;
    if (same_swing_(canonical_rx_, canonical_on_)) matches |= 1U << (axis + 1);
  }
  // Restore both the selected library candidate and the received ON packet even
  // when normalization fails. This function never sends IR or writes preferences.
  if (!apply_pairing_(candidate_) || !replace_swing_sample_(received_swing_)) {
    fault_("辨識擺風後恢復遙控器封包失敗；請重新檢查通訊");
    return;
  }
  base_needs_restore_ = false;
  if (reliable && (matches == 1 || matches == 2 || matches == 4)) {
    current_.swing = matches == 1 ? 0 : matches == 2 ? 1 : 2;
    current_.swing_from_remote = true;
  }
  ESP_LOGD("bc7215a", "Swing RX: reliable=%d matches=0x%02X state=%d (-1 unknown, 0 off, 1 vertical, 2 horizontal)",
           reliable, matches, current_.swing);
}
}  // namespace esphome::bc7215a
