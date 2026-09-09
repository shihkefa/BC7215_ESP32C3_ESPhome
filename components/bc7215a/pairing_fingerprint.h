#pragma once
#include <cstddef>
#include <cstdint>

namespace esphome::bc7215a {
// FNV-1a over a versioned, explicitly serialized record. This identifies saved
// pairing data, not a brand or a vendor protocol ID. Never hash struct padding.
template<typename Pairing> uint32_t pairing_fingerprint(const Pairing &pairing) {
  uint32_t hash = 2166136261u;
  auto byte = [&](uint8_t value) { hash = (hash ^ value) * 16777619u; };
  byte(1);  // Fingerprint serialization version.
  size_t length = 0;
  while (length < sizeof(pairing.version) && pairing.version[length]) length++;
  byte(static_cast<uint8_t>(length));
  for (size_t i = 0; i < length; i++) byte(pairing.version[i]);
  byte(pairing.match);
  byte(pairing.extra ? 1 : 0);
  auto capture = [&](const auto &record) {
    byte(record.count);
    for (size_t i = 0; i < record.count && i < 4; i++) {
      byte(record.status[i]);
      byte(record.format[i].signature.inByte);
      for (uint8_t value : record.format[i].format) byte(value);
      const uint16_t bits = record.data[i].bitLen;
      byte(static_cast<uint8_t>(bits));
      byte(static_cast<uint8_t>(bits >> 8));
      const size_t count = (size_t(bits) + 7) / 8;
      for (size_t j = 0; j < count && j < sizeof(record.data[i].data); j++)
        byte(record.data[i].data[j]);
    }
  };
  capture(pairing.base);
  if (pairing.extra) capture(pairing.second);
  return hash;
}
}  // namespace esphome::bc7215a
