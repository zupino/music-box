// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include "storage.h"

namespace musicbox {

/*
 * Fixed-memory stereo sample mixer. SD I/O happens only from service(), never
 * from AudioStream::update(). Each active voice owns a bounded ring buffer;
 * update() only interpolates/mixes already-buffered 16-bit stereo PCM.
 */
class SampleStream : public AudioStream {
 public:
  static constexpr uint8_t kVoiceCount = 16;
  static constexpr uint16_t kRingFrames = 768;

  explicit SampleStream(Storage *storage = nullptr);
  void setStorage(Storage *storage) { storage_ = storage; }

  bool play(uint8_t sampleSlot, const SampleMeta &meta, float rate = 1.0f,
            bool loop = false, uint8_t owner = 0xff, uint8_t gain = 127);
  void stopOwner(uint8_t owner);
  void stopAll();
  bool ownerActive(uint8_t owner) const;
  void service();
  uint32_t underruns() const { return underruns_; }
  virtual void update() override;

 private:
  struct Voice {
    bool active = false;
    bool looping = false;
    bool sourceEnded = false;
    uint8_t sampleSlot = 0xff;
    uint8_t owner = 0xff;
    uint8_t gain = 127;
    uint32_t startFrame = 0;
    uint32_t endFrame = 0;
    uint32_t sourceFrame = 0;
    uint32_t phaseQ16 = 0;
    uint32_t rateQ16 = 65536;
    volatile uint16_t head = 0;
    volatile uint16_t tail = 0;
    volatile uint16_t count = 0;
    File file;
    int16_t ring[kRingFrames][2];
  };

  Storage *storage_ = nullptr;
  Voice voices_[kVoiceCount];
  volatile uint32_t underruns_ = 0;

  int8_t chooseVoice(bool loop, uint8_t owner);
  void closeVoice(Voice &v);
  bool refill(Voice &v);
  static int16_t clamp16(int32_t v);
};

}  // namespace musicbox
