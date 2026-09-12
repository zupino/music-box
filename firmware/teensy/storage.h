// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "music_box_teensy.h"

namespace musicbox {
struct SampleMeta {
  bool present = false;
  uint32_t frames = 0;
  uint32_t trimStart = 0;
  uint32_t trimEnd = 0;
};

class Storage {
 public:
  bool begin();
  bool loadManifest(SampleMeta samples[kSlotCount], uint8_t assignments[kSlotCount]);
  bool saveManifest(const SampleMeta samples[kSlotCount], const uint8_t assignments[kSlotCount]);
  bool removeSample(uint8_t slot);
  void removeAll();
  File openSample(uint8_t slot, uint8_t mode = FILE_READ);
  File beginSampleWrite(uint8_t slot);
  bool commitSampleWrite(uint8_t slot, File &file);
  static void samplePath(uint8_t slot, char *out, size_t n, bool temp=false);
};
}
