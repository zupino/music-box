// SPDX-License-Identifier: MIT
#include "storage.h"

namespace musicbox {
static constexpr uint8_t kAudioShieldSdCs = 10;

void Storage::samplePath(uint8_t slot, char *out, size_t n, bool temp) {
  snprintf(out, n, temp ? "/musicbox/s%02u.tmp" : "/musicbox/s%02u.raw", unsigned(slot));
}

bool Storage::begin() {
#if defined(ARDUINO_TEENSY41)
  if (!SD.begin(BUILTIN_SDCARD)) return false;
#else
  // Teensy 4.0 has no onboard SDIO socket; use the Audio Adapter/external SPI SD.
  if (!SD.begin(kAudioShieldSdCs)) return false;
#endif
  if (!SD.exists("/musicbox")) SD.mkdir("/musicbox");
  return true;
}

File Storage::openSample(uint8_t slot, uint8_t mode) {
  char p[32]; samplePath(slot, p, sizeof(p)); return SD.open(p, mode);
}
File Storage::beginSampleWrite(uint8_t slot) {
  char p[32]; samplePath(slot,p,sizeof(p),true); SD.remove(p); return SD.open(p, FILE_WRITE);
}
bool Storage::commitSampleWrite(uint8_t slot, File &f) {
  f.flush(); f.close(); char tmp[32], dst[32]; samplePath(slot,tmp,sizeof(tmp),true); samplePath(slot,dst,sizeof(dst)); SD.remove(dst); return SD.rename(tmp,dst);
}
bool Storage::removeSample(uint8_t slot) { char p[32]; samplePath(slot,p,sizeof(p)); return !SD.exists(p) || SD.remove(p); }
void Storage::removeAll() { for (uint8_t i=0;i<kSlotCount;++i) removeSample(i); SD.remove("/musicbox/session.tmp"); SD.remove("/musicbox/session.txt"); }

bool Storage::loadManifest(SampleMeta s[kSlotCount], uint8_t a[kSlotCount]) {
  for (uint8_t i=0;i<kSlotCount;++i) { s[i]=SampleMeta{}; a[i]=0xff; }
  File f=SD.open("/musicbox/session.txt",FILE_READ); if(!f) return false;
  while(f.available()) {
    String line=f.readStringUntil('\n'); int slot, assigned; unsigned long frames,ts,te;
    if(sscanf(line.c_str(),"S %d %lu %lu %lu",&slot,&frames,&ts,&te)==4 && slot>=0 && slot<kSlotCount) { s[slot].present=true;s[slot].frames=frames;s[slot].trimStart=ts;s[slot].trimEnd=te; }
    else if(sscanf(line.c_str(),"A %d %d",&slot,&assigned)==2 && slot>=0 && slot<kSlotCount && assigned>=0 && assigned<kSlotCount) a[slot]=assigned;
  } f.close(); return true;
}
bool Storage::saveManifest(const SampleMeta s[kSlotCount], const uint8_t a[kSlotCount]) {
  SD.remove("/musicbox/session.tmp"); File f=SD.open("/musicbox/session.tmp",FILE_WRITE); if(!f)return false;
  f.println("MUSICBOX_TEENSY_SESSION 1");
  for(uint8_t i=0;i<kSlotCount;++i) if(s[i].present) f.printf("S %u %lu %lu %lu\n",i,(unsigned long)s[i].frames,(unsigned long)s[i].trimStart,(unsigned long)s[i].trimEnd);
  for(uint8_t i=0;i<kSlotCount;++i) if(a[i]<kSlotCount) f.printf("A %u %u\n",i,a[i]);
  f.flush();f.close();SD.remove("/musicbox/session.txt");return SD.rename("/musicbox/session.tmp","/musicbox/session.txt");
}
}
