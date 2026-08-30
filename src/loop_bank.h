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
    MUSIC_BOX_LOOP_STOPPED,
    /* Sampler only: play the stored audio once, then return to STOPPED. */
    MUSIC_BOX_LOOP_ONE_SHOT
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

/*
 * One bank of four stereo slots. The recording half of the state machine is
 * identical in both modes; the mode only decides what a finished recording and
 * a pad gesture mean. A loop bank starts repeating as soon as it is closed and
 * a pad press stops it. A sampler bank keeps the recording silent until its pad
 * is tapped, and then plays it exactly once.
 *
 * A sampler bank also carries a pre-roll: a ring of the most recent input,
 * long enough to cover the hold that arms the recording. A sample must span
 * exactly the time its pad was down, but the press only becomes a recording
 * gesture once the pad has been held, so the head of the sample has already
 * gone past by then. Copying the ring in at record start recovers it.
 */
typedef struct {
    MusicBoxLoopSlot slots[MUSIC_BOX_LOOP_COUNT];
    MusicBoxQuantizeMode quantize_mode;
    MusicBoxMode mode;
    float *preroll_left;
    float *preroll_right;
    size_t preroll_capacity;
    size_t preroll_position;
    size_t preroll_filled;
    MusicBoxEventQueue *events;
} MusicBoxLoopBank;

/* preroll_milliseconds of 0 leaves the bank without a pre-roll. */
bool music_box_loop_bank_init(MusicBoxLoopBank *bank,
                              uint32_t sample_rate,
                              uint32_t max_seconds,
                              MusicBoxQuantizeMode quantize_mode,
                              MusicBoxMode mode,
                              uint32_t preroll_milliseconds,
                              MusicBoxEventQueue *events);
void music_box_loop_bank_destroy(MusicBoxLoopBank *bank);

void music_box_loop_bank_pad_press(MusicBoxLoopBank *bank,
                                   uint8_t slot,
                                   const MusicBoxTransport *transport);
/* Sampler banks arm a recording here instead of on the press, so that a short
   tap stays available for triggering the stored sample. Loop banks ignore it. */
void music_box_loop_bank_pad_hold(MusicBoxLoopBank *bank,
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
