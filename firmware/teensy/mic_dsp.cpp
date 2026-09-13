// SPDX-License-Identifier: MIT
#include "mic_dsp.h"
#include <math.h>
#include <string.h>

namespace musicbox {

MicDsp::MicDsp() : AudioStream(4, inputQueueArray_) {
  const float sr = AUDIO_SAMPLE_RATE_EXACT;
  gateAttack_ = expf(-1000.0f / (5.0f * sr));
  gateRelease_ = expf(-1000.0f / (120.0f * sr));
}

void MicDsp::reset() {
  AudioNoInterrupts();
  memset(historyL_,0,sizeof(historyL_)); memset(historyR_,0,sizeof(historyR_));
  memset(ll_,0,sizeof(ll_)); memset(rl_,0,sizeof(rl_)); memset(lr_,0,sizeof(lr_)); memset(rr_,0,sizeof(rr_));
  writeIndex_=0; gateGain_=1.0f;
  AudioInterrupts();
}

int16_t MicDsp::toInt16(float x) {
  if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
  return static_cast<int16_t>(x * 32767.0f);
}

void MicDsp::process(float micL, float micR, float refL, float refR, float &cleanL, float &cleanR) {
  if (!aecEnabled_) { cleanL=micL; cleanR=micR; }
  else {
    const uint16_t write=writeIndex_;
    historyL_[write]=refL; historyR_[write]=refR;
    uint16_t index=static_cast<uint16_t>((write + kHistory - kDelayFrames) % kHistory);
    float estL=0.0f, estR=0.0f, refEnergy=0.0f;
    for(uint16_t tap=0;tap<kTaps;++tap){
      const float hl=historyL_[index], hr=historyR_[index];
      estL += ll_[tap]*hl + rl_[tap]*hr;
      estR += lr_[tap]*hl + rr_[tap]*hr;
      refEnergy += hl*hl + hr*hr;
      index = index==0 ? kHistory-1 : index-1;
    }
    const float errL=micL-estL, errR=micR-estR;
    const float micEnergy=micL*micL+micR*micR;
    const float meanRef=refEnergy/float(kTaps);
    constexpr float kMinimum=1.0e-8f;
    constexpr float kStep=0.05f;
    constexpr float kDoubleTalk=4.0f;
    if(refEnergy>kMinimum && micEnergy<=meanRef*kDoubleTalk+kMinimum){
      const float scaleL=kStep*errL/(refEnergy+kMinimum);
      const float scaleR=kStep*errR/(refEnergy+kMinimum);
      index=static_cast<uint16_t>((write + kHistory - kDelayFrames) % kHistory);
      for(uint16_t tap=0;tap<kTaps;++tap){
        const float hl=historyL_[index], hr=historyR_[index];
        ll_[tap]+=scaleL*hl; rl_[tap]+=scaleL*hr;
        lr_[tap]+=scaleR*hl; rr_[tap]+=scaleR*hr;
        index=index==0?kHistory-1:index-1;
      }
    }
    cleanL=errL; cleanR=errR;
    writeIndex_=static_cast<uint16_t>((write+1)%kHistory);
  }

  if(gateEnabled_){
    const float level=fmaxf(fabsf(cleanL),fabsf(cleanR));
    const float target=level>=0.015f?1.0f:0.10f;
    const float coeff=target>gateGain_?gateAttack_:gateRelease_;
    gateGain_=target+coeff*(gateGain_-target);
    cleanL*=gateGain_; cleanR*=gateGain_;
  }
}

void MicDsp::update() {
  audio_block_t *micL=receiveReadOnly(0), *micR=receiveReadOnly(1);
  audio_block_t *refL=receiveReadOnly(2), *refR=receiveReadOnly(3);
  if(!micL || !micR){
    if(micL)release(micL); if(micR)release(micR); if(refL)release(refL); if(refR)release(refR);
    return;
  }
  audio_block_t *outL=allocate(), *outR=allocate();
  if(!outL || !outR){
    if(outL)release(outL); if(outR)release(outR);
    release(micL); release(micR); if(refL)release(refL); if(refR)release(refR); return;
  }
  for(uint16_t i=0;i<AUDIO_BLOCK_SAMPLES;++i){
    const float ml=micL->data[i]/32768.0f, mr=micR->data[i]/32768.0f;
    const float rl=refL?refL->data[i]/32768.0f:0.0f, rr=refR?refR->data[i]/32768.0f:0.0f;
    float cl,cr; process(ml,mr,rl,rr,cl,cr);
    outL->data[i]=toInt16(cl); outR->data[i]=toInt16(cr);
  }
  transmit(outL,0); transmit(outR,1);
  release(outL); release(outR); release(micL); release(micR); if(refL)release(refL); if(refR)release(refR);
}

}  // namespace musicbox
