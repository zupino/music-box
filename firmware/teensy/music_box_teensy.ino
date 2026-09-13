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
#include "engine.h"

using namespace musicbox;

USBHost usbHost;
USBHub hub1(usbHost);
MIDIDevice_BigBuffer midi(usbHost);
Adafruit_SSD1306 oled(128,32,&Wire,-1);
AudioInputI2S i2sIn;
AudioMixer4 outL, outR;
AudioOutputI2S i2sOut;
AudioConnection patch1(i2sIn,0,outL,0), patch2(i2sIn,1,outR,0);
AudioConnection patch3(outL,0,i2sOut,0), patch4(outR,0,i2sOut,1);
AudioControlSGTL5000 codec;
Storage storage;
Engine engine;
static uint8_t lastVolume=255;

static void onNoteOn(byte ch, byte note, byte velocity){(void)ch;engine.noteOn(note,velocity);}
static void onNoteOff(byte ch, byte note, byte velocity){(void)ch;(void)velocity;engine.noteOff(note);}
static void onCC(byte ch, byte cc, byte value){(void)ch;engine.control(cc,value);if(cc==kVolumeCc){float v=0.02f+0.78f*(float(value)/127.0f);codec.volume(v);lastVolume=value;}}

static void drawUi(){
  static uint32_t next=0;if(int32_t(millis()-next)<0)return;next=millis()+50;
  oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(SSD1306_WHITE);oled.setCursor(0,0);
  static const char *names[]={"LOOP","SAMPLE","PAD","MELODY","FILES"};oled.print(names[uint8_t(engine.mode())]);
  oled.setCursor(72,0);oled.print("S");oled.print(engine.selectedSample()+1);
  const SampleMeta *s=engine.samples();
  for(uint8_t i=0;i<16;++i){uint8_t x=(i%4)*12,y=12+(i/4)*5;if(s[i].present)oled.fillRect(x,y,8,3,SSD1306_WHITE);else oled.drawRect(x,y,8,3,SSD1306_WHITE);}
  oled.display();
}

void setup(){
  Serial.begin(115200);AudioMemory(80);Wire.begin();
  codec.enable();codec.inputSelect(AUDIO_INPUT_LINEIN);codec.lineInLevel(5);codec.volume(0.55f);
  outL.gain(0,0.8f);outR.gain(0,0.8f);
  if(!storage.begin())Serial.println("music-box: SD unavailable");
  engine.begin(&storage);
  if(oled.begin(SSD1306_SWITCHCAPVCC,0x3c)){oled.clearDisplay();oled.display();}
  usbHost.begin();midi.setHandleNoteOn(onNoteOn);midi.setHandleNoteOff(onNoteOff);midi.setHandleControlChange(onCC);
  Serial.println("music-box Teensy target ready");
}
void loop(){usbHost.Task();while(midi.read()){}engine.service();drawUi();yield();}
