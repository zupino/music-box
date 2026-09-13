// SPDX-License-Identifier: MIT
#include "engine.h"
#include <math.h>

namespace musicbox {

bool Engine::begin(Storage *s, SampleStream *player, StereoRecorder *recorder, BassInstrument *instrument) {
  storage_=s; player_=player; recorder_=recorder; instrument_=instrument;
  storage_->loadManifest(samples_,assignments_);
  return true;
}

void Engine::persist(){ storage_->saveManifest(samples_,assignments_); }

uint64_t Engine::beatFrames() const {
  return static_cast<uint64_t>((AUDIO_SAMPLE_RATE_EXACT * 60.0f / kDefaultBpm) + 0.5f);
}

uint64_t Engine::barFrames() const {
  return beatFrames() * kBeatsPerBar;
}

uint64_t Engine::nextBarFrame() const {
  if(!player_) return 0;
  const uint64_t now=player_->frameCounter();
  const uint64_t bar=barFrames();
  return ((now / bar) + 1u) * bar;
}

uint8_t Engine::beat() const {
  if(!player_) return 0;
  const uint64_t beatLength=beatFrames();
  if(!beatLength) return 0;
  return static_cast<uint8_t>((player_->frameCounter()/beatLength)%kBeatsPerBar);
}

uint8_t Engine::recordingSlot() const {
  for(uint8_t i=0;i<kSlotCount;++i) if(recording_[i]) return i;
  return 0xff;
}

uint16_t Engine::loadedMask() const {
  uint16_t mask=0;
  for(uint8_t i=0;i<kSlotCount;++i) if(samples_[i].present) mask|=uint16_t(1u<<i);
  return mask;
}

uint16_t Engine::playingLoopMask() const {
  uint16_t mask=0;
  for(uint8_t i=0;i<kSlotCount;++i) if(loopActive_[i]) mask|=uint16_t(1u<<i);
  return mask;
}

uint16_t Engine::pendingLoopMask() const {
  uint16_t mask=0;
  for(uint8_t i=0;i<kSlotCount;++i) if(loopTogglePending_[i]) mask|=uint16_t(1u<<i);
  return mask;
}

void Engine::noteOn(uint8_t note,uint8_t velocity){
  int8_t slot=noteToSlot(note);
  if(slot<0){ if(mode_==Mode::Pad) triggerInstrument(note,velocity); return; }
  held_[slot]=true; pressedAt_[slot]=millis();
  if(mode_==Mode::Pad) triggerInstrument(note,velocity);
  else if(mode_==Mode::Melody && samples_[selectedSample_].present) {
    const float semitones=float(int(slot)-8);
    triggerSample(selectedSample_,powf(2.0f,semitones/12.0f));
  }
}

void Engine::noteOff(uint8_t note){
  if(mode_==Mode::Pad){ if(instrument_)instrument_->noteOff(note); return; }
  int8_t slot=noteToSlot(note); if(slot<0)return;
  uint32_t held=millis()-pressedAt_[slot];held_[slot]=false;
  if(held>=kPadHoldMs)longRelease(slot);else shortPress(slot);
}

void Engine::shortPress(uint8_t slot){
  switch(mode_){
    case Mode::Loop: {
      if(!player_ || loopTogglePending_[slot]) break;
      const uint64_t boundary=nextBarFrame();
      loopBoundaryFrame_[slot]=boundary;
      if(loopActive_[slot]) {
        player_->stopOwnerAt(slot,boundary);
        loopTargetActive_[slot]=false;
        loopTogglePending_[slot]=true;
      } else {
        const uint8_t sample=assignments_[slot]<kSlotCount?assignments_[slot]:slot;
        if(samples_[sample].present && player_->playAt(sample,samples_[sample],boundary,1.0f,true,slot)) {
          loopTargetActive_[slot]=true;
          loopTogglePending_[slot]=true;
        }
      }
      break;
    }
    case Mode::Sampler:
      if(samples_[slot].present)triggerSample(slot);
      break;
    case Mode::Files:
      deleteSlot(slot);
      break;
    default:break;
  }
}

void Engine::longRelease(uint8_t slot){
  switch(mode_){
    case Mode::Sampler:
      if(recording_[slot])finishRecording(slot);
      break;
    case Mode::Loop:
      if(samples_[selectedSample_].present){assignments_[slot]=selectedSample_;persist();}
      break;
    case Mode::Files:
      deleteAll();
      break;
    default:break;
  }
}

void Engine::control(uint8_t cc,uint8_t value){
  if(cc==kModeCc){mode_=ccToMode(value);return;}
  if(cc==kVolumeCc){volume_=value;return;}
  if(cc==kSampleCc){selectedSample_=min<uint8_t>(kSlotCount-1,(uint16_t(value)*kSlotCount)/128);return;}
  if(cc==kTrimStartCc){trimStart_=value;applyTrim();return;}
  if(cc==kTrimEndCc){trimEnd_=value;applyTrim();return;}
  if(cc==kMonitorCc){
    const bool high=value>=64;
    if(high && !monitorCcHigh_){
      if(monitorRecording_) finishRecording(monitorSlot_);
      else {
        for(uint8_t i=0;i<kSlotCount;++i) if(!samples_[i].present){monitorSlot_=i;startRecording(i,true);break;}
      }
    }
    monitorCcHigh_=high;
  }
}

void Engine::applyTrim(){
  if(!samples_[selectedSample_].present)return;
  auto &s=samples_[selectedSample_];
  s.trimStart=(uint64_t(s.frames)*trimStart_)/127u;
  s.trimEnd=(uint64_t(s.frames)*trimEnd_)/127u;
  if(s.trimEnd<=s.trimStart)s.trimEnd=min(s.frames,s.trimStart+1);
  persist();
}

void Engine::deleteSlot(uint8_t slot){
  if(player_) player_->stopOwner(slot);
  loopActive_[slot]=false; loopTogglePending_[slot]=false;
  storage_->removeSample(slot);samples_[slot]=SampleMeta{};
  for(auto &a:assignments_)if(a==slot)a=0xff;
  persist();
}

void Engine::deleteAll(){
  if(player_) player_->stopAll();
  storage_->removeAll();
  for(auto &s:samples_)s=SampleMeta{};
  for(auto &a:assignments_)a=0xff;
  for(bool &v:loopActive_)v=false;
  for(bool &v:loopTogglePending_)v=false;
  persist();
}

void Engine::serviceLoopBoundaries(uint64_t nowFrame){
  for(uint8_t slot=0;slot<kSlotCount;++slot){
    if(!loopTogglePending_[slot] || nowFrame<loopBoundaryFrame_[slot]) continue;
    loopActive_[slot]=loopTargetActive_[slot];
    loopTogglePending_[slot]=false;
  }
}

void Engine::service(){
  if(recorder_) recorder_->service();
  if(player_) {
    player_->service();
    serviceLoopBoundaries(player_->frameCounter());
  }
  const uint32_t now=millis();
  for(uint8_t i=0;i<kSlotCount;++i){
    if(held_[i] && mode_==Mode::Sampler && !recording_[i] && now-pressedAt_[i]>=kPadHoldMs) startRecording(i,false);
  }
}

void Engine::startRecording(uint8_t slot,bool monitor){
  if(!recorder_ || recorder_->active())return;
  if(recorder_->start(slot,monitor)){
    recording_[slot]=true;
    if(monitor){monitorRecording_=true;monitorSlot_=slot;}
  }
}

void Engine::finishRecording(uint8_t slot){
  if(!recording_[slot] || !recorder_)return;
  uint32_t frames=0;
  if(recorder_->stop(&frames) && frames>0){
    samples_[slot].present=true;
    samples_[slot].frames=frames;
    samples_[slot].trimStart=0;
    samples_[slot].trimEnd=frames;
    selectedSample_=slot;
  }
  recording_[slot]=false;
  if(slot==monitorSlot_){monitorRecording_=false;monitorSlot_=0xff;}
  persist();
}

void Engine::triggerSample(uint8_t slot,float rate){
  if(player_ && slot<kSlotCount && samples_[slot].present) player_->play(slot,samples_[slot],rate,false,0xff);
}

void Engine::triggerInstrument(uint8_t note,uint8_t velocity){
  if(instrument_)instrument_->noteOn(note,velocity);
}

}  // namespace musicbox
