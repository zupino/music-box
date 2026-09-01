// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_MELODY_H
#define MUSIC_BOX_MELODY_H

#include "loop_bank.h"

typedef struct {
    bool active;
    uint8_t sample_slot;
    float position;
    float increment;
} MusicBoxMelodyVoice;

typedef struct {
    MusicBoxMelodyVoice voices[MUSIC_BOX_LOOP_COUNT];
    MusicBoxEventQueue *events;
} MusicBoxMelody;

void music_box_melody_init(MusicBoxMelody *melody, MusicBoxEventQueue *events);
bool music_box_melody_trigger(MusicBoxMelody *melody, uint8_t pad,
                              uint8_t sample_slot,
                              const MusicBoxLoopBank *samples);
void music_box_melody_mix_sample(MusicBoxMelody *melody,
                                 const MusicBoxLoopBank *samples,
                                 float *left, float *right);

#endif
