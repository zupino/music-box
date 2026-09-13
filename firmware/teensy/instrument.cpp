// SPDX-License-Identifier: MIT
#include "instrument.h"
#include <math.h>

namespace musicbox {

BassInstrument::BassInstrument(AudioSynthWaveform *oscillators[kVoices], AudioEffectEnvelope *envelopes[kVoices]) {
  for(uint8_t i=0;i<kVoices;++i){osc_[i]=oscillators[i];env_[i]=envelopes[i];}
}

float BassInstrument::midiFrequency(uint8_t note){
  return 440.0f*powf(2.0f,(float(note)-69.0f)/12.0f);
}

uint8_t BassInstrument::chooseVoice(){
  for(uint8_t i=0;i<kVoices;++i) if(note_[i]==0xff) return i;
  uint8_t oldest=0;for(uint8_t i=1;i<kVoices;++i)if(age_[i]<age_[oldest])oldest=i;
  env_[oldest]->noteOff();return oldest;
}

void BassInstrument::begin(){
  for(uint8_t i=0;i<kVoices;++i){
    osc_[i]->begin(WAVEFORM_TRIANGLE);
    osc_[i]->amplitude(0.45f);
    osc_[i]->frequency(110.0f);
    env_[i]->attack(4.0f);env_[i]->hold(0.0f);env_[i]->decay(110.0f);env_[i]->sustain(0.58f);env_[i]->release(180.0f);
  }
}

void BassInstrument::noteOn(uint8_t note,uint8_t velocity){
  const uint8_t v=chooseVoice();
  note_[v]=note;age_[v]=counter_++;
  osc_[v]->frequency(midiFrequency(note));
  osc_[v]->amplitude(0.15f+0.35f*(float(velocity)/127.0f));
  env_[v]->noteOn();
}

void BassInstrument::noteOff(uint8_t note){
  for(uint8_t i=0;i<kVoices;++i) if(note_[i]==note){env_[i]->noteOff();note_[i]=0xff;}
}

}  // namespace musicbox
