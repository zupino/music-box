// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_AUDIO_ENGINE_H
#define MUSIC_BOX_AUDIO_ENGINE_H

#include "config.h"
#include "echo_cancel.h"
#include "loop_bank.h"
#include "noise_gate.h"
#include "rt_queue.h"
#include "transport.h"

#include <jack/jack.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct {
    jack_client_t *client;
    jack_port_t *microphone_left;
    jack_port_t *microphone_right;
    jack_port_t *synth_left;
    jack_port_t *synth_right;
    jack_port_t *output_left;
    jack_port_t *output_right;
    MusicBoxConfig config;
    MusicBoxTransport transport;
    MusicBoxLoopBank loop_bank;
    MusicBoxLoopBank sampler_bank;
    MusicBoxMode mode;
    uint8_t selected_loop;
    uint8_t selected_sample;
    MusicBoxRecordingSource recording_source;
    /* The bank each pad was pressed on, so a mode change mid-gesture cannot
       route the matching release to the other bank and strand a recording. */
    MusicBoxMode pad_bank[MUSIC_BOX_LOOP_COUNT];
    MusicBoxEchoCanceller echo_canceller;
    MusicBoxNoiseGate noise_gate;
    MusicBoxCommandQueue *commands;
    MusicBoxEventQueue *events;
    atomic_bool jack_shutdown;
} MusicBoxAudioEngine;

bool music_box_audio_engine_init(MusicBoxAudioEngine *engine,
                                 const MusicBoxConfig *config,
                                 MusicBoxCommandQueue *commands,
                                 MusicBoxEventQueue *events);
void music_box_audio_engine_destroy(MusicBoxAudioEngine *engine);
void music_box_audio_engine_connect_ports(MusicBoxAudioEngine *engine);
bool music_box_audio_engine_jack_shutdown(const MusicBoxAudioEngine *engine);
uint32_t music_box_audio_engine_sample_rate(const MusicBoxAudioEngine *engine);

#endif
