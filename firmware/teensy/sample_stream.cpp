// SPDX-License-Identifier: MIT
#include "sample_stream.h"
#include <math.h>

namespace musicbox {

SampleStream::SampleStream(Storage *storage) : AudioStream(0, nullptr), storage_(storage) {}

int16_t SampleStream::clamp16(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return static_cast<int16_t>(v);
}

void SampleStream::closeVoice(Voice &v) {
  AudioNoInterrupts();
  v.active = false;
  v.count = 0;
  v.head = v.tail = 0;
  AudioInterrupts();
  if (v.file) v.file.close();
  v.sourceEnded = false;
  v.sampleSlot = 0xff;
  v.owner = 0xff;
}

int8_t SampleStream::chooseVoice(bool loop, uint8_t owner) {
  if (loop && owner < kVoiceCount) {
    closeVoice(voices_[owner]);
    return static_cast<int8_t>(owner);
  }
  for (uint8_t i = 0; i < kVoiceCount; ++i) if (!voices_[i].active) return i;
  for (uint8_t i = 0; i < kVoiceCount; ++i) if (!voices_[i].looping) { closeVoice(voices_[i]); return i; }
  closeVoice(voices_[0]);
  return 0;
}

bool SampleStream::play(uint8_t sampleSlot, const SampleMeta &meta, float rate,
                        bool loop, uint8_t owner, uint8_t gain) {
  if (!storage_ || !meta.present || meta.frames == 0) return false;
  if (!(rate > 0.05f && rate < 8.0f)) return false;
  const uint32_t start = min(meta.trimStart, meta.frames - 1);
  const uint32_t end = (meta.trimEnd > start && meta.trimEnd <= meta.frames) ? meta.trimEnd : meta.frames;
  if (end <= start) return false;

  const int8_t index = chooseVoice(loop, owner);
  Voice &v = voices_[index];
  v.file = storage_->openSample(sampleSlot, FILE_READ);
  if (!v.file) return false;
  if (!v.file.seek(start * 4u)) { v.file.close(); return false; }

  v.looping = loop;
  v.sampleSlot = sampleSlot;
  v.owner = owner;
  v.gain = gain;
  v.startFrame = start;
  v.endFrame = end;
  v.sourceFrame = start;
  v.phaseQ16 = 0;
  v.rateQ16 = static_cast<uint32_t>(rate * 65536.0f + 0.5f);
  v.head = v.tail = v.count = 0;
  v.sourceEnded = false;
  v.active = true;

  for (uint8_t n = 0; n < 3 && v.count < kRingFrames / 2; ++n) refill(v);
  return v.count >= 2;
}

void SampleStream::stopOwner(uint8_t owner) {
  for (auto &v : voices_) if (v.active && v.owner == owner) closeVoice(v);
}

void SampleStream::stopAll() {
  for (auto &v : voices_) closeVoice(v);
}

bool SampleStream::ownerActive(uint8_t owner) const {
  for (const auto &v : voices_) if (v.active && v.owner == owner) return true;
  return false;
}

bool SampleStream::refill(Voice &v) {
  if (!v.active || !v.file) return false;

  uint16_t count;
  AudioNoInterrupts(); count = v.count; AudioInterrupts();
  if (count >= kRingFrames - 64) return false;

  if (v.sourceFrame >= v.endFrame) {
    if (!v.looping) { v.sourceEnded = true; return false; }
    if (!v.file.seek(v.startFrame * 4u)) { v.sourceEnded = true; return false; }
    v.sourceFrame = v.startFrame;
  }

  uint16_t tail;
  AudioNoInterrupts(); tail = v.tail; count = v.count; AudioInterrupts();
  const uint16_t freeFrames = kRingFrames - count;
  const uint16_t contiguous = min<uint16_t>(freeFrames, kRingFrames - tail);
  const uint32_t remaining = v.endFrame - v.sourceFrame;
  const uint16_t want = min<uint16_t>(contiguous, min<uint32_t>(remaining, 192u));
  if (!want) return false;

  const int bytes = v.file.read(reinterpret_cast<uint8_t *>(&v.ring[tail][0]), static_cast<size_t>(want) * 4u);
  if (bytes <= 0) { v.sourceEnded = true; return false; }
  const uint16_t frames = static_cast<uint16_t>(bytes / 4);
  if (!frames) { v.sourceEnded = true; return false; }

  AudioNoInterrupts();
  v.tail = static_cast<uint16_t>((tail + frames) % kRingFrames);
  v.count = static_cast<uint16_t>(v.count + frames);
  AudioInterrupts();
  v.sourceFrame += frames;
  return true;
}

void SampleStream::service() {
  for (auto &v : voices_) {
    if (!v.active) continue;
    for (uint8_t n = 0; n < 2; ++n) if (!refill(v)) break;
    uint16_t count; bool ended;
    AudioNoInterrupts(); count = v.count; ended = v.sourceEnded; AudioInterrupts();
    if (ended && count < 2) closeVoice(v);
  }
}

void SampleStream::update() {
  audio_block_t *left = allocate();
  audio_block_t *right = allocate();
  if (!left || !right) {
    if (left) release(left);
    if (right) release(right);
    ++underruns_;
    return;
  }
  memset(left->data, 0, sizeof(left->data));
  memset(right->data, 0, sizeof(right->data));

  for (uint16_t out = 0; out < AUDIO_BLOCK_SAMPLES; ++out) {
    int32_t mixL = 0, mixR = 0;
    for (auto &v : voices_) {
      if (!v.active) continue;
      if (v.count < 2) { if (!v.sourceEnded) ++underruns_; continue; }

      const uint16_t i0 = v.head;
      const uint16_t i1 = static_cast<uint16_t>((i0 + 1) % kRingFrames);
      const uint32_t frac = v.phaseQ16 & 0xffffu;
      const int32_t l0 = v.ring[i0][0], l1 = v.ring[i1][0];
      const int32_t r0 = v.ring[i0][1], r1 = v.ring[i1][1];
      const int32_t l = l0 + static_cast<int32_t>(((int64_t)(l1 - l0) * frac) >> 16);
      const int32_t r = r0 + static_cast<int32_t>(((int64_t)(r1 - r0) * frac) >> 16);
      mixL += (l * v.gain) / 127;
      mixR += (r * v.gain) / 127;

      uint32_t phase = v.phaseQ16 + v.rateQ16;
      uint16_t advance = static_cast<uint16_t>(phase >> 16);
      v.phaseQ16 = phase & 0xffffu;
      if (advance) {
        if (advance >= v.count) advance = v.count > 1 ? static_cast<uint16_t>(v.count - 1) : 0;
        v.head = static_cast<uint16_t>((v.head + advance) % kRingFrames);
        v.count = static_cast<uint16_t>(v.count - advance);
      }
    }
    left->data[out] = clamp16(mixL);
    right->data[out] = clamp16(mixR);
  }

  transmit(left, 0); transmit(right, 1);
  release(left); release(right);
}

}  // namespace musicbox
