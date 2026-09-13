// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <Audio.h>

namespace musicbox {

class BassInstrument {
 public:
  static constexpr uint8_t kVoices = 4;
  BassInstrument(AudioSynthWaveform *oscillators[kVoices], AudioEffectEnvelope *envelopes[kVoices]);
  void begin();
  void noteOn(uint8_t note, uint8_t velocity);
  void noteOff(uint8_t note);
 private:
  AudioSynthWaveform *osc_[kVoices];
  AudioEffectEnvelope *env_[kVoices];
  uint8_t note_[kVoices]{0xff,0xff,0xff,0xff};
  uint32_t age_[kVoices]{};
  uint32_t counter_=1;
  static float midiFrequency(uint8_t note);
  uint8_t chooseVoice();
};

}  // namespace musicbox
