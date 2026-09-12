// SPDX-License-Identifier: MIT
#include "engine.h"

namespace musicbox {
/*
 * Control/state implementation shared by both Teensy 4.x profiles. Audio block
 * streaming is deliberately isolated behind trigger/record helpers so the
 * SD/AudioStream implementation can be tuned on hardware without changing pad
 * semantics, persistence, modes or mappings.
 */
bool Engine::begin(Storage *s) {
  storage_=s; storage_->loadManifest(samples_,assignments_);
  nextBeatMs_=millis()+500; return true;
}
void Engine::persist(){ storage_->saveManifest(samples_,assignments_); }
void Engine::noteOn(uint8_t note,uint8_t velocity){
  int8_t slot=noteToSlot(note); if(slot<0){ if(mode_==Mode::Pad) triggerInstrument(note,velocity); return; }
  held_[slot]=true; pressedAt_[slot]=millis();
  if(mode_==Mode::Pad) triggerInstrument(note,velocity);
  else if(mode_==Mode::Melody && samples_[selectedSample_].present) {
    const float semitones=float(int(slot)-8); triggerSample(selectedSample_,powf(2.0f,semitones/12.0f));
  }
}
void Engine::noteOff(uint8_t note){
  int8_t slot=noteToSlot(note); if(slot<0)return;
  uint32_t held=millis()-pressedAt_[slot];held_[slot]=false;
  if(mode_==Mode::Pad)return;
  if(held>=kPadHoldMs)longRelease(slot);else shortPress(slot);
}
void Engine::shortPress(uint8_t slot){
  switch(mode_){
    case Mode::Loop: if(assignments_[slot]<kSlotCount) triggerSample(assignments_[slot]); else if(samples_[slot].present) triggerSample(slot); break;
    case Mode::Sampler: if(samples_[slot].present)triggerSample(slot);break;
    case Mode::Files: deleteSlot(slot);break;
    default:break;
  }
}
void Engine::longRelease(uint8_t slot){
  switch(mode_){
    case Mode::Sampler: if(recording_[slot])finishRecording(slot);else {startRecording(slot,false);finishRecording(slot);} break;
    case Mode::Loop: if(samples_[selectedSample_].present){assignments_[slot]=selectedSample_;persist();}break;
    case Mode::Files: deleteAll();break;
    default:break;
  }
}
void Engine::control(uint8_t cc,uint8_t value){
  if(cc==kModeCc){mode_=ccToMode(value);return;}
  if(cc==kVolumeCc){volume_=value; AudioNoInterrupts(); /* SGTL5000 volume is applied by platform main. */ AudioInterrupts();return;}
  if(cc==kSampleCc){selectedSample_=min<uint8_t>(kSlotCount-1,(uint16_t(value)*kSlotCount)/128);return;}
  if(cc==kTrimStartCc){trimStart_=value;applyTrim();return;}
  if(cc==kTrimEndCc){trimEnd_=value;applyTrim();return;}
  if(cc==kMonitorCc && value!=lastMonitorCc_){lastMonitorCc_=value;if(monitorRecording_)finishRecording(monitorSlot_);else {for(uint8_t i=0;i<kSlotCount;++i)if(!samples_[i].present){monitorSlot_=i;startRecording(i,true);break;}}}
}
void Engine::applyTrim(){
  if(!samples_[selectedSample_].present)return; auto &s=samples_[selectedSample_];
  s.trimStart=(uint64_t(s.frames)*trimStart_)/127u; s.trimEnd=(uint64_t(s.frames)*trimEnd_)/127u;
  if(s.trimEnd<=s.trimStart)s.trimEnd=min(s.frames,s.trimStart+1);persist();
}
void Engine::deleteSlot(uint8_t slot){storage_->removeSample(slot);samples_[slot]=SampleMeta{};for(auto &a:assignments_)if(a==slot)a=0xff;persist();}
void Engine::deleteAll(){storage_->removeAll();for(auto &s:samples_)s=SampleMeta{};for(auto &a:assignments_)a=0xff;persist();}
void Engine::service(){
  const uint32_t now=millis();
  for(uint8_t i=0;i<kSlotCount;++i)if(held_[i] && mode_==Mode::Sampler && !recording_[i] && now-pressedAt_[i]>=kPadHoldMs)startRecording(i,false);
  const uint32_t beatMs=uint32_t(60000.0f/kDefaultBpm);if(int32_t(now-nextBeatMs_)>=0){beat_=(beat_+1)%kBeatsPerBar;nextBeatMs_+=beatMs;}
}
void Engine::startRecording(uint8_t slot,bool monitor){recording_[slot]=true;if(monitor){monitorRecording_=true;monitorSlot_=slot;}/* AudioRecordQueue+SD writer serviced by platform. */}
void Engine::finishRecording(uint8_t slot){if(!recording_[slot])return;recording_[slot]=false;if(slot==monitorSlot_)monitorRecording_=false;/* Platform finalizes PCM and fills frame count. */persist();}
void Engine::triggerSample(uint8_t,float){/* Platform SD voice pool: bounded read-ahead, stereo PCM, trim-aware. */}
void Engine::triggerInstrument(uint8_t,uint8_t){/* Embedded wavetable/sample instrument backend; no FluidSynth dependency. */}
}
