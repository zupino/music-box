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

static void onNoteOn(byte ch, byte note, byte velocity){(void)ch;engine.noteOn(note,velocity);}
static void onNoteOff(byte ch, byte note, byte velocity){(void)ch;(void)velocity;engine.noteOff(note);}
static void onCC(byte ch, byte cc, byte value){
  (void)ch;engine.control(cc,value);
  if(cc==kVolumeCc){float v=0.02f+0.78f*(float(value)/127.0f);codec.volume(v);}
}

static void drawUi(){
  static uint32_t next=0;if(int32_t(millis()-next)<0)return;next=millis()+50;
  oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(SSD1306_WHITE);oled.setCursor(0,0);
  static const char *names[]={"LOOP","SAMPLE","PAD","MELODY","FILES"};oled.print(names[uint8_t(engine.mode())]);
  oled.setCursor(72,0);oled.print("S");oled.print(engine.selectedSample()+1);
  oled.setCursor(105,0);oled.print(engine.beat()+1);
  const SampleMeta *s=engine.samples();
  for(uint8_t i=0;i<16;++i){uint8_t x=(i%8)*15,y=12+(i/8)*8;if(s[i].present)oled.fillRect(x,y,10,4,SSD1306_WHITE);else oled.drawRect(x,y,10,4,SSD1306_WHITE);}
  if(engine.recording()){oled.setCursor(88,24);oled.print("REC");}
  oled.display();
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
  yield();
}
