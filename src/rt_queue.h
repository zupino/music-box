// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_RT_QUEUE_H
#define MUSIC_BOX_RT_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define MUSIC_BOX_QUEUE_CAPACITY 64u

/*
 * Performance mode. It selects which pad bank the MPD218 pads drive, and the
 * same value identifies the bank that produced a status event.
 */
typedef enum {
    MUSIC_BOX_MODE_LOOP = 0,
    MUSIC_BOX_MODE_SAMPLER
} MusicBoxMode;

#define MUSIC_BOX_MODE_COUNT 2u

typedef enum {
    MUSIC_BOX_COMMAND_PAD_PRESS = 1,
    /* Emitted once by the MIDI thread when a pad stays down long enough. */
    MUSIC_BOX_COMMAND_PAD_HOLD,
    MUSIC_BOX_COMMAND_PAD_RELEASE,
    MUSIC_BOX_COMMAND_SET_TEMPO,
    MUSIC_BOX_COMMAND_SET_MODE
} MusicBoxCommandType;

typedef struct {
    MusicBoxCommandType type;
    uint8_t slot;
    uint8_t mode;
    uint32_t tempo_millibpm;
} MusicBoxCommand;

typedef enum {
    MUSIC_BOX_EVENT_RECORD_ARMED = 1,
    MUSIC_BOX_EVENT_RECORD_STARTED,
    MUSIC_BOX_EVENT_RECORD_FINISHED,
    MUSIC_BOX_EVENT_LOOP_STOP_ARMED,
    MUSIC_BOX_EVENT_LOOP_STOP_CANCELLED,
    MUSIC_BOX_EVENT_LOOP_STOPPED,
    MUSIC_BOX_EVENT_LOOP_FULL,
    MUSIC_BOX_EVENT_SAMPLE_STARTED,
    MUSIC_BOX_EVENT_SAMPLE_FINISHED,
    MUSIC_BOX_EVENT_TEMPO_CHANGED,
    MUSIC_BOX_EVENT_MODE_CHANGED,
    MUSIC_BOX_EVENT_BEAT
} MusicBoxEventType;

typedef struct {
    MusicBoxEventType type;
    uint8_t slot;
    /* MusicBoxMode of the bank that produced the event. */
    uint8_t bank;
    uint64_t value;
} MusicBoxEvent;

typedef struct {
    MusicBoxCommand items[MUSIC_BOX_QUEUE_CAPACITY];
    atomic_uint read_index;
    atomic_uint write_index;
} MusicBoxCommandQueue;

typedef struct {
    MusicBoxEvent items[MUSIC_BOX_QUEUE_CAPACITY];
    atomic_uint read_index;
    atomic_uint write_index;
} MusicBoxEventQueue;

void music_box_command_queue_init(MusicBoxCommandQueue *queue);
bool music_box_command_queue_push(MusicBoxCommandQueue *queue,
                                  const MusicBoxCommand *command);
bool music_box_command_queue_pop(MusicBoxCommandQueue *queue,
                                 MusicBoxCommand *command);

void music_box_event_queue_init(MusicBoxEventQueue *queue);
bool music_box_event_queue_push(MusicBoxEventQueue *queue,
                                const MusicBoxEvent *event);
bool music_box_event_queue_pop(MusicBoxEventQueue *queue,
                               MusicBoxEvent *event);

#endif
