// SPDX-License-Identifier: MIT
#pragma once

#include <Arduino.h>

namespace musicbox {
constexpr uint8_t kSlotCount = 16;
constexpr uint8_t kModeCount = 5;
constexpr uint8_t kModeCc = 9;
constexpr uint8_t kVolumeCc = 3;
constexpr uint8_t kMonitorCc = 12;
constexpr uint8_t kSampleCc = 13;
constexpr uint8_t kTrimStartCc = 14;
constexpr uint8_t kTrimEndCc = 15;
constexpr uint32_t kPadHoldMs = 400;
constexpr float kDefaultBpm = 120.0f;
constexpr uint8_t kBeatsPerBar = 4;
constexpr uint8_t kPadNotes[kSlotCount] = {
  64,65,66,67, 60,61,62,63, 56,57,58,59, 52,53,54,55
};

enum class Mode : uint8_t { Loop=0, Sampler, Pad, Melody, Files };

inline int8_t noteToSlot(uint8_t note) {
  for (uint8_t i=0; i<kSlotCount; ++i) if (kPadNotes[i] == note) return i;
  return -1;
}

inline Mode ccToMode(uint8_t value) {
  uint8_t zone = (uint16_t(value) * kModeCount) / 128u;
  if (zone >= kModeCount) zone = kModeCount - 1;
  return static_cast<Mode>(zone);
}
}
