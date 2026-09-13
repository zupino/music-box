// SPDX-License-Identifier: MIT
#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <USBHost_t36.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "music_box_teensy.h"
#include "storage.h"
#include "sample_stream.h"
#include "recorder.h"
#include "mic_dsp.h"
#include "instrument.h"
#include "engine.h"

using namespace musicbox;

USBHost usbHost;
USBHub hub1(usbHost);
MIDIDevice_BigBuffer midi(usbHost);
Adafruit_SSD1306 oled(128,32,&Wire,-1);

AudioInputI2S i2sIn;
SampleStream sampleStream;
MicDsp micDsp;
AudioSynthWaveform synthOsc0, synthOsc1, synthOsc2, synthOsc3;
AudioEffectEnvelope synthEnv0, synthEnv1, synthEnv2, synthEnv3;
AudioMixer4 synthMixer;
AudioMixer4 outL, outR;
AudioOutputI2S i2sOut;
AudioRecordQueue micQueueL, micQueueR, monitorQueueL, monitorQueueR;

AudioConnection patchSampleL(sampleStream,0,outL,0);
AudioConnection patchSampleR(sampleStream,1,outR,0);
AudioConnection patchSynth0(synthOsc0,0,synthEnv0,0);
AudioConnection patchSynth1(synthOsc1,0,synthEnv1,0);
AudioConnection patchSynth2(synthOsc2,0,synthEnv2,0);
AudioConnection patchSynth3(synthOsc3,0,synthEnv3,0);
AudioConnection patchSynthMix0(synthEnv0,0,synthMixer,0);
AudioConnection patchSynthMix1(synthEnv1,0,synthMixer,1);
AudioConnection patchSynthMix2(synthEnv2,0,synthMixer,2);
AudioConnection patchSynthMix3(synthEnv3,0,synthMixer,3);
AudioConnection patchSynthL(synthMixer,0,outL,1);
AudioConnection patchSynthR(synthMixer,0,outR,1);
AudioConnection patchOutL(outL,0,i2sOut,0);
AudioConnection patchOutR(outR,0,i2sOut,1);
AudioConnection patchMicDspL(i2sIn,0,micDsp,0);
AudioConnection patchMicDspR(i2sIn,1,micDsp,1);
AudioConnection patchRefL(outL,0,micDsp,2);
AudioConnection patchRefR(outR,0,micDsp,3);
AudioConnection patchMicQueueL(micDsp,0,micQueueL,0);
AudioConnection patchMicQueueR(micDsp,1,micQueueR,0);
AudioConnection patchMonitorQueueL(outL,0,monitorQueueL,0);
AudioConnection patchMonitorQueueR(outR,0,monitorQueueR,0);

AudioControlSGTL5000 codec;
Storage storage;
StereoRecorder recorder(&storage,&micQueueL,&micQueueR,&monitorQueueL,&monitorQueueR);
AudioSynthWaveform *synthOscillators[BassInstrument::kVoices]={&synthOsc0,&synthOsc1,&synthOsc2,&synthOsc3};
AudioEffectEnvelope *synthEnvelopes[BassInstrument::kVoices]={&synthEnv0,&synthEnv1,&synthEnv2,&synthEnv3};
BassInstrument instrument(synthOscillators,synthEnvelopes);
Engine engine;
static bool sdReady=false;
static uint32_t volumeOverlayUntil=0;

static void onNoteOn(byte ch, byte note, byte velocity){(void)ch;engine.noteOn(note,velocity);}
static void onNoteOff(byte ch, byte note, byte velocity){(void)ch;(void)velocity;engine.noteOff(note);}
static void onCC(byte ch, byte cc, byte value){
  (void)ch;engine.control(cc,value);
  if(cc==kVolumeCc){
    float v=0.02f+0.78f*(float(value)/127.0f);
    codec.volume(v);
    volumeOverlayUntil=millis()+900;
  }
}

static void drawPadGrid(){
  const uint16_t loaded=engine.loadedMask();
  const uint16_t playing=engine.playingLoopMask();
  const uint16_t pending=engine.pendingLoopMask();
  const uint8_t rec=engine.recordingSlot();
  const bool blink=((millis()/250u)&1u)==0;
  for(uint8_t slot=0;slot<16;++slot){
    const uint8_t x=3+(slot%4)*9;
    const uint8_t y=(slot/4)*8;
    const uint16_t bit=uint16_t(1u<<slot);
    if(rec==slot){
      if(blink) oled.fillRect(x,y,7,7,SSD1306_WHITE);
      else oled.drawRect(x,y,7,7,SSD1306_WHITE);
    }else if((pending&bit)!=0){
      oled.drawRect(x,y,7,7,SSD1306_WHITE);
      oled.drawLine(x+1,y+1,x+5,y+5,SSD1306_WHITE);
      oled.drawLine(x+5,y+1,x+1,y+5,SSD1306_WHITE);
    }else if((playing&bit)!=0){
      oled.fillRect(x,y,7,7,SSD1306_WHITE);
    }else if((loaded&bit)!=0){
      oled.drawRect(x,y,7,7,SSD1306_WHITE);
    }else{
      oled.drawFastHLine(x,y+6,7,SSD1306_WHITE);
    }
  }
}

static void drawBeatRail(){
  const uint8_t current=engine.beat();
  for(uint8_t beat=0;beat<4;++beat){
    const uint8_t y=1+beat*8;
    if(beat==current) oled.fillRect(122,y,5,5,SSD1306_WHITE);
    else oled.fillRect(123,y+1,3,3,SSD1306_WHITE);
  }
}

static void drawStatus(){
  static const char *names[]={"LOOP","SAMPLR","PAD","MELODY","FILES"};
  oled.drawFastVLine(40,0,32,SSD1306_WHITE);
  oled.setCursor(44,0);oled.print(names[uint8_t(engine.mode())]);
  oled.setCursor(44,11);oled.print("120 BPM");
  oled.setCursor(44,22);
  if(engine.recording()){
    oled.print("REC ");oled.print(engine.recordingMonitor()?"MON":"MIC");
    const uint8_t slot=engine.recordingSlot();if(slot<16){oled.print(" ");oled.print(slot+1);}
  }else if(engine.underruns()>0 || engine.droppedRecordBlocks()>0){
    oled.print("U");oled.print(engine.underruns());oled.print(" D");oled.print(engine.droppedRecordBlocks());
  }else if(engine.mode()==Mode::Loop || engine.mode()==Mode::Melody || engine.mode()==Mode::Sampler){
    oled.print("S");oled.print(engine.selectedSample()+1);
  }else if(!sdReady){
    oled.print("NO SD");
  }
}

static void drawVolumeOverlay(){
  oled.fillRect(42,9,77,22,SSD1306_BLACK);
  oled.drawRect(44,13,70,8,SSD1306_WHITE);
  const uint8_t width=uint8_t((uint16_t(engine.volume())*66u)/127u);
  if(width) oled.fillRect(46,15,width,4,SSD1306_WHITE);
  oled.setCursor(44,23);oled.print("VOL ");oled.print((uint16_t(engine.volume())*100u)/127u);oled.print("%");
}

static void drawUi(){
  static uint32_t next=0;if(int32_t(millis()-next)<0)return;next=millis()+50;
  oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(SSD1306_WHITE);
  drawPadGrid();drawStatus();drawBeatRail();
  if(int32_t(volumeOverlayUntil-millis())>0)drawVolumeOverlay();
  oled.display();
}

static void serviceDiagnostics(){
  static uint32_t next=0;
  const uint32_t now=millis();
  if(int32_t(now-next)<0)return;
  next=now+2000;
  Serial.print("music-box diag: underruns=");Serial.print(engine.underruns());
  Serial.print(" dropped_record_blocks=");Serial.print(engine.droppedRecordBlocks());
  Serial.print(" audio_mem=");Serial.print(AudioMemoryUsage());
  Serial.print(" audio_mem_max=");Serial.println(AudioMemoryUsageMax());
}

void setup(){
  Serial.begin(115200);
  AudioMemory(180);
  Wire.begin();
  codec.enable();codec.inputSelect(AUDIO_INPUT_LINEIN);codec.lineInLevel(5);codec.volume(0.55f);codec.lineOutLevel(13);
  for(uint8_t i=0;i<4;++i)synthMixer.gain(i,0.24f);
  outL.gain(0,0.72f);outL.gain(1,0.55f);outR.gain(0,0.72f);outR.gain(1,0.55f);
  instrument.begin();
  sdReady=storage.begin();
  if(!sdReady)Serial.println("music-box: SD unavailable");
  sampleStream.setStorage(&storage);
  recorder.begin();
  engine.begin(&storage,&sampleStream,&recorder,&instrument);
  if(oled.begin(SSD1306_SWITCHCAPVCC,0x3c)){oled.clearDisplay();oled.display();}
  usbHost.begin();midi.setHandleNoteOn(onNoteOn);midi.setHandleNoteOff(onNoteOff);midi.setHandleControlChange(onCC);
  Serial.println("music-box Teensy target ready");
}

void loop(){
  usbHost.Task();
  while(midi.read()){}
  engine.service();
  drawUi();
  serviceDiagnostics();
  yield();
}
