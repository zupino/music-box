// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <Audio.h>

namespace musicbox {

/* Stereo NLMS echo cancellation + linked stereo noise gate.
 * Inputs: 0/1 microphone L/R, 2/3 exact speaker reference L/R.
 * Outputs: 0/1 cleaned microphone L/R.
 */
class MicDsp : public AudioStream {
 public:
  static constexpr uint16_t kTaps = 128;
  static constexpr uint16_t kDelayFrames = 512;
  static constexpr uint16_t kHistory = kTaps + kDelayFrames + 1;

  MicDsp();
  void reset();
  void setAecEnabled(bool enabled) { aecEnabled_ = enabled; }
  void setGateEnabled(bool enabled) { gateEnabled_ = enabled; }
  virtual void update() override;

 private:
  audio_block_t *inputQueueArray_[4];
  bool aecEnabled_ = true;
  bool gateEnabled_ = true;
  uint16_t writeIndex_ = 0;
  float historyL_[kHistory]{};
  float historyR_[kHistory]{};
  float ll_[kTaps]{}, rl_[kTaps]{}, lr_[kTaps]{}, rr_[kTaps]{};
  float gateGain_ = 1.0f;
  float gateAttack_ = 0.0f;
  float gateRelease_ = 0.0f;

  void process(float micL, float micR, float refL, float refR, float &cleanL, float &cleanR);
  static int16_t toInt16(float x);
};

}  // namespace musicbox
