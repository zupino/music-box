// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <Audio.h>
#include "storage.h"
#include "sample_stream.h"
#include "recorder.h"
#include "instrument.h"

namespace musicbox {
class Engine {
 public:
  bool begin(Storage *storage, SampleStream *player, StereoRecorder *recorder, BassInstrument *instrument);
  void service();
  void noteOn(uint8_t note, uint8_t velocity);
  void noteOff(uint8_t note);
  void control(uint8_t cc, uint8_t value);
  Mode mode() const { return mode_; }
  uint8_t selectedSample() const { return selectedSample_; }
  const SampleMeta *samples() const { return samples_; }
  uint8_t beat() const;
  uint8_t volume() const { return volume_; }
  bool recording() const { return recorder_ && recorder_->active(); }
  bool recordingMonitor() const { return monitorRecording_; }
  uint8_t recordingSlot() const;
  uint16_t loadedMask() const;
  uint16_t playingLoopMask() const;
  uint16_t pendingLoopMask() const;
  uint32_t underruns() const { return player_ ? player_->underruns() : 0; }
  uint32_t droppedRecordBlocks() const { return recorder_ ? recorder_->droppedBlocks() : 0; }
 private:
  Storage *storage_=nullptr;
  SampleStream *player_=nullptr;
  StereoRecorder *recorder_=nullptr;
  BassInstrument *instrument_=nullptr;
  SampleMeta samples_[kSlotCount];
  uint8_t assignments_[kSlotCount];
  Mode mode_=Mode::Pad;
  uint8_t selectedSample_=0;
  uint8_t volume_=96;
  uint8_t trimStart_=0, trimEnd_=127;
  bool held_[kSlotCount]{};
  uint32_t pressedAt_[kSlotCount]{};
  bool recording_[kSlotCount]{};
  bool loopActive_[kSlotCount]{};
  bool loopTogglePending_[kSlotCount]{};
  bool loopTargetActive_[kSlotCount]{};
  uint64_t loopBoundaryFrame_[kSlotCount]{};
  uint8_t monitorSlot_=0xff;
  bool monitorRecording_=false;
  bool monitorCcHigh_=false;

  void shortPress(uint8_t slot);
  void longRelease(uint8_t slot);
  void deleteSlot(uint8_t slot);
  void deleteAll();
  void persist();
  void applyTrim();
  void startRecording(uint8_t slot, bool monitor);
  void finishRecording(uint8_t slot);
  void triggerSample(uint8_t slot, float rate=1.0f);
  void triggerInstrument(uint8_t note, uint8_t velocity);
  uint64_t beatFrames() const;
  uint64_t barFrames() const;
  uint64_t nextBarFrame() const;
  void serviceLoopBoundaries(uint64_t nowFrame);
};
}
