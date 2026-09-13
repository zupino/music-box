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
  if (!SD.begin(kAudioShieldSdCs)) return false;
#endif
  if (!SD.exists("/musicbox")) SD.mkdir("/musicbox");

  // Recover only complete atomic manifest writes; incomplete sample temp files
  // are never referenced and are safe to discard after a reset/power loss.
  if (!SD.exists("/musicbox/session.txt") && SD.exists("/musicbox/session.tmp"))
    SD.rename("/musicbox/session.tmp", "/musicbox/session.txt");
  else
    SD.remove("/musicbox/session.tmp");
  for (uint8_t i=0;i<kSlotCount;++i) {
    char p[32]; samplePath(i,p,sizeof(p),true); SD.remove(p);
  }
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
    if(sscanf(line.c_str(),"S %d %lu %lu %lu",&slot,&frames,&ts,&te)==4 && slot>=0 && slot<kSlotCount) {
      char p[32]; samplePath(static_cast<uint8_t>(slot),p,sizeof(p)); File sample=SD.open(p,FILE_READ);
      if(sample){
        const uint32_t fileFrames=static_cast<uint32_t>(sample.size()/4u);sample.close();
        if(fileFrames){
          const uint32_t safeFrames=min<uint32_t>(static_cast<uint32_t>(frames),fileFrames);
          s[slot].present=true;s[slot].frames=safeFrames;
          s[slot].trimStart=min<uint32_t>(static_cast<uint32_t>(ts),safeFrames-1);
          s[slot].trimEnd=min<uint32_t>(static_cast<uint32_t>(te),safeFrames);
          if(s[slot].trimEnd<=s[slot].trimStart)s[slot].trimEnd=safeFrames;
        }
      }
    } else if(sscanf(line.c_str(),"A %d %d",&slot,&assigned)==2 && slot>=0 && slot<kSlotCount && assigned>=0 && assigned<kSlotCount) a[slot]=assigned;
  }
  f.close();
  for(uint8_t i=0;i<kSlotCount;++i) if(a[i]<kSlotCount && !s[a[i]].present) a[i]=0xff;
  return true;
}

bool Storage::saveManifest(const SampleMeta s[kSlotCount], const uint8_t a[kSlotCount]) {
  SD.remove("/musicbox/session.tmp"); File f=SD.open("/musicbox/session.tmp",FILE_WRITE); if(!f)return false;
  f.println("MUSICBOX_TEENSY_SESSION 1");
  for(uint8_t i=0;i<kSlotCount;++i) if(s[i].present) f.printf("S %u %lu %lu %lu\n",i,(unsigned long)s[i].frames,(unsigned long)s[i].trimStart,(unsigned long)s[i].trimEnd);
  for(uint8_t i=0;i<kSlotCount;++i) if(a[i]<kSlotCount) f.printf("A %u %u\n",i,a[i]);
  f.flush();f.close();SD.remove("/musicbox/session.txt");return SD.rename("/musicbox/session.tmp","/musicbox/session.txt");
}
}
