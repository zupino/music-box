// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <Audio.h>
#include "storage.h"

namespace musicbox {

class StereoRecorder {
 public:
  static constexpr uint32_t kSampleRate = AUDIO_SAMPLE_RATE_EXACT;
  static constexpr uint32_t kMaxFrames = static_cast<uint32_t>(AUDIO_SAMPLE_RATE_EXACT * 10.0f);
  static constexpr uint16_t kPreRollFrames = static_cast<uint16_t>(AUDIO_SAMPLE_RATE_EXACT * 0.400f);

  StereoRecorder(Storage *storage, AudioRecordQueue *micL, AudioRecordQueue *micR,
                 AudioRecordQueue *monitorL, AudioRecordQueue *monitorR);
  void begin();
  void service();
  bool start(uint8_t slot, bool monitorSource);
  bool stop(uint32_t *framesWritten = nullptr);
  bool active() const { return active_; }
  uint8_t activeSlot() const { return slot_; }
  bool monitorSource() const { return monitorSource_; }
  uint32_t droppedBlocks() const { return droppedBlocks_; }

 private:
  Storage *storage_;
  AudioRecordQueue *micL_;
  AudioRecordQueue *micR_;
  AudioRecordQueue *monitorL_;
  AudioRecordQueue *monitorR_;
  File file_;
  bool active_ = false;
  bool monitorSource_ = false;
  uint8_t slot_ = 0xff;
  uint32_t frames_ = 0;
  uint32_t droppedBlocks_ = 0;

  int16_t preRoll_[kPreRollFrames][2];
  uint16_t preHead_ = 0;
  uint16_t preCount_ = 0;

  void servicePair(AudioRecordQueue *left, AudioRecordQueue *right, bool microphonePair);
  void rememberMicBlock(const int16_t *left, const int16_t *right);
  bool writeBlock(const int16_t *left, const int16_t *right, uint16_t frames);
  bool writePreRoll();
};

}  // namespace musicbox
