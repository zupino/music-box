// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_MIDI_H
#define MUSIC_BOX_MIDI_H

#include "config.h"
#include "rt_queue.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct {
    pthread_t thread;
    atomic_bool running;
    bool thread_started;
    MusicBoxConfig config;
    MusicBoxCommandQueue *commands;
} MusicBoxMidi;

bool music_box_midi_start(MusicBoxMidi *midi,
                          const MusicBoxConfig *config,
                          MusicBoxCommandQueue *commands);
void music_box_midi_stop(MusicBoxMidi *midi);

#endif
