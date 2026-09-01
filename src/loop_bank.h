// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_LOOP_BANK_H
#define MUSIC_BOX_LOOP_BANK_H

#include "rt_queue.h"
#include "transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The MPD218's full 4x4 pad grid. Slot 0 is printed PAD 1 at top-left;
   configuration maps the factory preset's opposite MIDI note-row order. */
#define MUSIC_BOX_LOOP_COUNT 16u
#define MUSIC_BOX_PAD_COLUMNS 4u
#define MUSIC_BOX_PAD_ROWS (MUSIC_BOX_LOOP_COUNT / MUSIC_BOX_PAD_COLUMNS)

typedef enum {
    MUSIC_BOX_LOOP_EMPTY = 0,
    MUSIC_BOX_LOOP_ARMED_RECORD,
    MUSIC_BOX_LOOP_RECORDING,
    MUSIC_BOX_LOOP_ARMED_FINISH,
    MUSIC_BOX_LOOP_PLAYING,
    MUSIC_BOX_LOOP_ARMED_STOP,
    MUSIC_BOX_LOOP_STOPPED,
    /* Sampler only: play the stored audio once, then return to STOPPED. */
    MUSIC_BOX_LOOP_ONE_SHOT,
    MUSIC_BOX_LOOP_ARMED_PLAY
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
    /* Assigned loops play once per whole bar. After reaching the sample tail,
       they remain active but silent until the next bar boundary. */
    bool waiting_for_bar;
    size_t trim_start;
    size_t trim_end;
    const float *playback_left;
    const float *playback_right;
    /* Sampler slot referenced by an assigned loop, or UINT8_MAX. */
    uint8_t assigned_sample;
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
 *
 * The ring only retains input captured while the bank itself was silent. What
 * the bank plays reaches the microphones, and the echo canceller only reduces
 * that bleed rather than removing it, so keeping those frames would paste the
 * tail of the outgoing sample onto the head of its replacement.
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
    /* Set by the last mix when any slot of this bank contributed audio. */
    bool sounding;
    MusicBoxRecordingSource recording_source;
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
bool music_box_loop_bank_assign(MusicBoxLoopBank *loops, uint8_t loop_slot,
                                const MusicBoxLoopBank *samples, uint8_t sample_slot);
/* Maps one of `division_count` knob positions across recorded samples only.
   Returns false when the bank has no usable recording. */
bool music_box_loop_bank_select_recorded(const MusicBoxLoopBank *bank,
                                         uint8_t position,
                                         uint8_t division_count,
                                         uint8_t *slot);
void music_box_loop_bank_stop_all(MusicBoxLoopBank *bank);
void music_box_loop_bank_set_trim(MusicBoxLoopBank *bank, uint8_t slot,
                                  bool start, uint8_t controller_value);
void music_box_loop_bank_sync_assignments(MusicBoxLoopBank *loops,
                                          const MusicBoxLoopBank *samples,
                                          uint8_t sample_slot);
void music_box_loop_bank_set_recording_source(MusicBoxLoopBank *bank,
                                              MusicBoxRecordingSource source);
/* Starts an immediate, pre-roll-free monitor take in the first empty sampler
   slot. Returns -1 when no slot is available and -2 when another recording is
   already active. */
int music_box_loop_bank_start_monitor_recording(MusicBoxLoopBank *bank,
                                                const MusicBoxTransport *transport);
bool music_box_loop_bank_finish_recording(MusicBoxLoopBank *bank, uint8_t slot);

#endif
