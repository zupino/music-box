// SPDX-License-Identifier: MIT
#include "engine.h"
#include <math.h>

namespace musicbox {

bool Engine::begin(Storage *s, SampleStream *player, StereoRecorder *recorder, BassInstrument *instrument) {
  storage_=s; player_=player; recorder_=recorder; instrument_=instrument;
  storage_->loadManifest(samples_,assignments_);
  nextBeatMs_=millis()+uint32_t(60000.0f/kDefaultBpm);
  return true;
}

void Engine::persist(){ storage_->saveManifest(samples_,assignments_); }

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
    case Mode::Loop:
      loopTogglePending_[slot]=true;
      break;
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

void Engine::applyLoopBoundary(){
  for(uint8_t slot=0;slot<kSlotCount;++slot){
    if(!loopTogglePending_[slot])continue;
    loopTogglePending_[slot]=false;
    if(loopActive_[slot]){
      player_->stopOwner(slot);loopActive_[slot]=false;continue;
    }
    uint8_t sample=assignments_[slot]<kSlotCount?assignments_[slot]:slot;
    if(samples_[sample].present && player_->play(sample,samples_[sample],1.0f,true,slot)) loopActive_[slot]=true;
  }
}

void Engine::service(){
  if(recorder_) recorder_->service();
  if(player_) player_->service();
  const uint32_t now=millis();
  for(uint8_t i=0;i<kSlotCount;++i){
    if(held_[i] && mode_==Mode::Sampler && !recording_[i] && now-pressedAt_[i]>=kPadHoldMs) startRecording(i,false);
  }
  const uint32_t beatMs=uint32_t(60000.0f/kDefaultBpm);
  while(int32_t(now-nextBeatMs_)>=0){
    beat_=uint8_t((beat_+1)%kBeatsPerBar);
    nextBeatMs_+=beatMs;
    if(beat_==0)applyLoopBoundary();
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
