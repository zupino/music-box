// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_MIDI_H
#define MUSIC_BOX_MIDI_H

#include "config.h"
#include "rt_queue.h"
#include "volume.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct {
    pthread_t thread;
    atomic_bool running;
    bool thread_started;
    MusicBoxConfig config;
    MusicBoxCommandQueue *commands;
    MusicBoxVolume *volume;
    /* Long-press tracking. Only the MIDI thread touches these. */
    uint64_t pad_press_milliseconds[MUSIC_BOX_LOOP_COUNT];
    bool pad_held[MUSIC_BOX_LOOP_COUNT];
    bool pad_hold_sent[MUSIC_BOX_LOOP_COUNT];
    MusicBoxMode mode;
} MusicBoxMidi;

bool music_box_midi_start(MusicBoxMidi *midi,
                          const MusicBoxConfig *config,
                          MusicBoxCommandQueue *commands,
                          MusicBoxVolume *volume);
void music_box_midi_stop(MusicBoxMidi *midi);

#endif
