// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_LOOP_BANK_H
#define MUSIC_BOX_LOOP_BANK_H

#include "rt_queue.h"
#include "transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSIC_BOX_LOOP_COUNT 4u

typedef enum {
    MUSIC_BOX_LOOP_EMPTY = 0,
    MUSIC_BOX_LOOP_ARMED_RECORD,
    MUSIC_BOX_LOOP_RECORDING,
    MUSIC_BOX_LOOP_ARMED_FINISH,
    MUSIC_BOX_LOOP_PLAYING,
    MUSIC_BOX_LOOP_ARMED_STOP,
    MUSIC_BOX_LOOP_STOPPED
} MusicBoxLoopState;

typedef struct {
    float *left;
    float *right;
    size_t capacity_frames;
    size_t recorded_frames;
    size_t play_position;
    uint64_t record_start_frame;
    MusicBoxLoopState state;
    bool finish_after_start;
} MusicBoxLoopSlot;

typedef struct {
    MusicBoxLoopSlot slots[MUSIC_BOX_LOOP_COUNT];
    MusicBoxQuantizeMode quantize_mode;
    MusicBoxEventQueue *events;
} MusicBoxLoopBank;

bool music_box_loop_bank_init(MusicBoxLoopBank *bank,
                              uint32_t sample_rate,
                              uint32_t max_loop_seconds,
                              MusicBoxQuantizeMode quantize_mode,
                              MusicBoxEventQueue *events);
void music_box_loop_bank_destroy(MusicBoxLoopBank *bank);

void music_box_loop_bank_pad_press(MusicBoxLoopBank *bank,
                                   uint8_t slot,
                                   const MusicBoxTransport *transport);
void music_box_loop_bank_pad_release(MusicBoxLoopBank *bank,
                                     uint8_t slot,
                                     const MusicBoxTransport *transport);
void music_box_loop_bank_apply_boundary(MusicBoxLoopBank *bank,
                                        const MusicBoxTransport *transport);
void music_box_loop_bank_mix_sample(MusicBoxLoopBank *bank,
                                    float *left,
                                    float *right);
void music_box_loop_bank_record_sample(MusicBoxLoopBank *bank,
                                       float left,
                                       float right);

#endif
