// SPDX-License-Identifier: MIT
#include "recorder.h"

namespace musicbox {

StereoRecorder::StereoRecorder(Storage *storage, AudioRecordQueue *micL, AudioRecordQueue *micR,
                               AudioRecordQueue *monitorL, AudioRecordQueue *monitorR)
    : storage_(storage), micL_(micL), micR_(micR), monitorL_(monitorL), monitorR_(monitorR) {}

void StereoRecorder::begin() {
  micL_->begin(); micR_->begin(); monitorL_->begin(); monitorR_->begin();
}

void StereoRecorder::rememberMicBlock(const int16_t *left, const int16_t *right) {
  for (uint16_t i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
    const uint16_t pos = static_cast<uint16_t>((preHead_ + preCount_) % kPreRollFrames);
    preRoll_[pos][0] = left[i]; preRoll_[pos][1] = right[i];
    if (preCount_ < kPreRollFrames) ++preCount_;
    else preHead_ = static_cast<uint16_t>((preHead_ + 1) % kPreRollFrames);
  }
}

bool StereoRecorder::writeBlock(const int16_t *left, const int16_t *right, uint16_t frames) {
  if (!file_) return false;
  int16_t interleaved[AUDIO_BLOCK_SAMPLES * 2];
  const uint16_t n = min<uint32_t>(frames, kMaxFrames - frames_);
  for (uint16_t i = 0; i < n; ++i) {
    interleaved[i * 2] = left[i]; interleaved[i * 2 + 1] = right[i];
  }
  const size_t bytes = static_cast<size_t>(n) * 4u;
  if (file_.write(reinterpret_cast<const uint8_t *>(interleaved), bytes) != bytes) return false;
  frames_ += n;
  return true;
}

bool StereoRecorder::writePreRoll() {
  if (!preCount_) return true;
  int16_t l[AUDIO_BLOCK_SAMPLES], r[AUDIO_BLOCK_SAMPLES];
  uint16_t remaining = preCount_, cursor = preHead_;
  while (remaining) {
    const uint16_t n = min<uint16_t>(AUDIO_BLOCK_SAMPLES, remaining);
    for (uint16_t i = 0; i < n; ++i) {
      const uint16_t p = static_cast<uint16_t>((cursor + i) % kPreRollFrames);
      l[i] = preRoll_[p][0]; r[i] = preRoll_[p][1];
    }
    if (!writeBlock(l, r, n)) return false;
    cursor = static_cast<uint16_t>((cursor + n) % kPreRollFrames);
    remaining = static_cast<uint16_t>(remaining - n);
  }
  return true;
}

bool StereoRecorder::start(uint8_t slot, bool monitorSource) {
  if (active_ || slot >= kSlotCount) return false;
  file_ = storage_->beginSampleWrite(slot);
  if (!file_) return false;
  slot_ = slot; frames_ = 0; monitorSource_ = monitorSource; active_ = true;
  if (!monitorSource_ && !writePreRoll()) { stop(); return false; }
  return true;
}

bool StereoRecorder::stop(uint32_t *framesWritten) {
  if (!active_) return false;
  const uint8_t completedSlot = slot_;
  active_ = false;
  const bool ok = storage_->commitSampleWrite(completedSlot, file_);
  if (framesWritten) *framesWritten = frames_;
  slot_ = 0xff;
  return ok;
}

void StereoRecorder::servicePair(AudioRecordQueue *leftQ, AudioRecordQueue *rightQ, bool microphonePair) {
  while (leftQ->available() && rightQ->available()) {
    const int16_t *left = leftQ->readBuffer();
    const int16_t *right = rightQ->readBuffer();
    if (microphonePair) rememberMicBlock(left, right);
    if (active_ && monitorSource_ != microphonePair) {
      if (!writeBlock(left, right, AUDIO_BLOCK_SAMPLES)) ++droppedBlocks_;
      if (frames_ >= kMaxFrames) stop();
    }
    leftQ->freeBuffer(); rightQ->freeBuffer();
  }
  while (leftQ->available() > 8) { leftQ->freeBuffer(); ++droppedBlocks_; }
  while (rightQ->available() > 8) { rightQ->freeBuffer(); ++droppedBlocks_; }
}

void StereoRecorder::service() {
  servicePair(micL_, micR_, true);
  servicePair(monitorL_, monitorR_, false);
}

}  // namespace musicbox
