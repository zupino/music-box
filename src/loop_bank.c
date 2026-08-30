// SPDX-License-Identifier: MIT
#include "loop_bank.h"

#include <stdlib.h>
#include <string.h>

static void emit_event(MusicBoxLoopBank *bank,
                       MusicBoxEventType type,
                       uint8_t slot,
                       uint64_t value)
{
    if (bank->events == NULL) {
        return;
    }

    const MusicBoxEvent event = {
        .type = type,
        .slot = slot,
        .bank = (uint8_t)bank->mode,
        .value = value,
    };
    (void)music_box_event_queue_push(bank->events, &event);
}

static void stop_one_shot(MusicBoxLoopBank *bank, uint8_t slot_index)
{
    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    slot->state = MUSIC_BOX_LOOP_STOPPED;
    slot->play_position = 0u;
    emit_event(bank, MUSIC_BOX_EVENT_SAMPLE_FINISHED, slot_index, 0u);
}

static void start_recording(MusicBoxLoopBank *bank,
                            uint8_t slot_index,
                            const MusicBoxTransport *transport)
{
    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    slot->recorded_frames = 0u;
    slot->play_position = 0u;
    slot->record_start_frame = transport->frame_position;
    slot->state = MUSIC_BOX_LOOP_RECORDING;
    emit_event(bank,
               MUSIC_BOX_EVENT_RECORD_STARTED,
               slot_index,
               transport->frame_position);
}

static void finish_recording(MusicBoxLoopBank *bank, uint8_t slot_index)
{
    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    slot->finish_after_start = false;
    slot->play_position = 0u;
    if (slot->recorded_frames == 0u) {
        slot->state = MUSIC_BOX_LOOP_EMPTY;
        return;
    }

    /* A loop repeats immediately; a sample waits for the next pad tap. */
    slot->state = bank->mode == MUSIC_BOX_MODE_SAMPLER
                      ? MUSIC_BOX_LOOP_STOPPED
                      : MUSIC_BOX_LOOP_PLAYING;
    emit_event(bank,
               MUSIC_BOX_EVENT_RECORD_FINISHED,
               slot_index,
               slot->recorded_frames);
}

bool music_box_loop_bank_init(MusicBoxLoopBank *bank,
                              uint32_t sample_rate,
                              uint32_t max_seconds,
                              MusicBoxQuantizeMode quantize_mode,
                              MusicBoxMode mode,
                              MusicBoxEventQueue *events)
{
    if (bank == NULL || sample_rate == 0u || max_seconds == 0u) {
        return false;
    }

    memset(bank, 0, sizeof(*bank));
    bank->quantize_mode = quantize_mode;
    bank->mode = mode;
    bank->events = events;

    const size_t capacity =
        (size_t)sample_rate * (size_t)max_seconds;

    for (size_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        MusicBoxLoopSlot *slot = &bank->slots[index];
        slot->left = calloc(capacity, sizeof(float));
        slot->right = calloc(capacity, sizeof(float));
        slot->capacity_frames = capacity;
        slot->state = MUSIC_BOX_LOOP_EMPTY;

        if (slot->left == NULL || slot->right == NULL) {
            music_box_loop_bank_destroy(bank);
            return false;
        }
    }

    return true;
}

void music_box_loop_bank_destroy(MusicBoxLoopBank *bank)
{
    if (bank == NULL) {
        return;
    }

    for (size_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        free(bank->slots[index].left);
        free(bank->slots[index].right);
        bank->slots[index].left = NULL;
        bank->slots[index].right = NULL;
    }
}

static void arm_recording(MusicBoxLoopBank *bank,
                          uint8_t slot_index,
                          const MusicBoxTransport *transport)
{
    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    slot->finish_after_start = false;
    if (bank->quantize_mode == MUSIC_BOX_QUANTIZE_OFF) {
        start_recording(bank, slot_index, transport);
    } else {
        slot->state = MUSIC_BOX_LOOP_ARMED_RECORD;
        emit_event(bank, MUSIC_BOX_EVENT_RECORD_ARMED, slot_index, 0u);
    }
}

/*
 * A sampler pad triggers on the press so the sample is heard immediately. The
 * gesture that records instead arrives later as a hold.
 */
static void sampler_pad_press(MusicBoxLoopBank *bank, uint8_t slot_index)
{
    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    switch (slot->state) {
    case MUSIC_BOX_LOOP_STOPPED:
        if (slot->recorded_frames == 0u) {
            break;
        }
        slot->play_position = 0u;
        slot->state = MUSIC_BOX_LOOP_ONE_SHOT;
        emit_event(bank,
                   MUSIC_BOX_EVENT_SAMPLE_STARTED,
                   slot_index,
                   slot->recorded_frames);
        break;

    case MUSIC_BOX_LOOP_ONE_SHOT:
        /* Retrigger from the start; the pad already reads as playing. */
        slot->play_position = 0u;
        break;

    case MUSIC_BOX_LOOP_EMPTY:
    case MUSIC_BOX_LOOP_ARMED_RECORD:
    case MUSIC_BOX_LOOP_RECORDING:
    case MUSIC_BOX_LOOP_ARMED_FINISH:
    default:
        break;
    }
}

void music_box_loop_bank_pad_hold(MusicBoxLoopBank *bank,
                                  uint8_t slot_index,
                                  const MusicBoxTransport *transport)
{
    if (slot_index >= MUSIC_BOX_LOOP_COUNT ||
        bank->mode != MUSIC_BOX_MODE_SAMPLER) {
        return;
    }

    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    /* Recording over a sample silences the preview started by the press. */
    if (slot->state == MUSIC_BOX_LOOP_ONE_SHOT) {
        stop_one_shot(bank, slot_index);
    }
    if (slot->state != MUSIC_BOX_LOOP_EMPTY &&
        slot->state != MUSIC_BOX_LOOP_STOPPED) {
        return;
    }
    arm_recording(bank, slot_index, transport);
}

void music_box_loop_bank_pad_press(MusicBoxLoopBank *bank,
                                   uint8_t slot_index,
                                   const MusicBoxTransport *transport)
{
    if (slot_index >= MUSIC_BOX_LOOP_COUNT) {
        return;
    }

    if (bank->mode == MUSIC_BOX_MODE_SAMPLER) {
        sampler_pad_press(bank, slot_index);
        return;
    }

    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    switch (slot->state) {
    case MUSIC_BOX_LOOP_PLAYING:
        if (bank->quantize_mode == MUSIC_BOX_QUANTIZE_OFF) {
            slot->state = MUSIC_BOX_LOOP_STOPPED;
            slot->play_position = 0u;
            emit_event(bank, MUSIC_BOX_EVENT_LOOP_STOPPED, slot_index, 0u);
        } else {
            slot->state = MUSIC_BOX_LOOP_ARMED_STOP;
            emit_event(bank, MUSIC_BOX_EVENT_LOOP_STOP_ARMED, slot_index, 0u);
        }
        break;

    case MUSIC_BOX_LOOP_ARMED_STOP:
        /* A second press before the boundary cancels the pending stop. */
        slot->state = MUSIC_BOX_LOOP_PLAYING;
        break;

    case MUSIC_BOX_LOOP_EMPTY:
    case MUSIC_BOX_LOOP_STOPPED:
        arm_recording(bank, slot_index, transport);
        break;

    case MUSIC_BOX_LOOP_ARMED_RECORD:
    case MUSIC_BOX_LOOP_RECORDING:
    case MUSIC_BOX_LOOP_ARMED_FINISH:
    case MUSIC_BOX_LOOP_ONE_SHOT:
    default:
        /* Duplicate Note On events are ignored while recording is active. */
        break;
    }
}

void music_box_loop_bank_pad_release(MusicBoxLoopBank *bank,
                                     uint8_t slot_index,
                                     const MusicBoxTransport *transport)
{
    (void)transport;

    if (slot_index >= MUSIC_BOX_LOOP_COUNT) {
        return;
    }

    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    switch (slot->state) {
    case MUSIC_BOX_LOOP_ARMED_RECORD:
        /*
         * A quick tap still records one complete quantization interval. This
         * also keeps the physical pad tap before the actual recording start.
         */
        slot->finish_after_start = true;
        break;

    case MUSIC_BOX_LOOP_RECORDING:
        if (bank->quantize_mode == MUSIC_BOX_QUANTIZE_OFF) {
            finish_recording(bank, slot_index);
        } else {
            slot->state = MUSIC_BOX_LOOP_ARMED_FINISH;
        }
        break;

    case MUSIC_BOX_LOOP_ARMED_FINISH:
    case MUSIC_BOX_LOOP_PLAYING:
    case MUSIC_BOX_LOOP_ARMED_STOP:
    case MUSIC_BOX_LOOP_STOPPED:
    case MUSIC_BOX_LOOP_EMPTY:
    case MUSIC_BOX_LOOP_ONE_SHOT:
    default:
        /* A sample released before the hold threshold just keeps playing. */
        break;
    }
}

void music_box_loop_bank_apply_boundary(MusicBoxLoopBank *bank,
                                        const MusicBoxTransport *transport)
{
    if (!music_box_transport_is_boundary(transport, bank->quantize_mode) ||
        bank->quantize_mode == MUSIC_BOX_QUANTIZE_OFF) {
        return;
    }

    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        MusicBoxLoopSlot *slot = &bank->slots[index];

        switch (slot->state) {
        case MUSIC_BOX_LOOP_ARMED_RECORD:
            start_recording(bank, index, transport);
            break;

        case MUSIC_BOX_LOOP_ARMED_FINISH:
            if (transport->frame_position > slot->record_start_frame) {
                finish_recording(bank, index);
            }
            break;

        case MUSIC_BOX_LOOP_RECORDING:
            if (slot->finish_after_start &&
                transport->frame_position > slot->record_start_frame) {
                finish_recording(bank, index);
            }
            break;

        case MUSIC_BOX_LOOP_ARMED_STOP:
            slot->state = MUSIC_BOX_LOOP_STOPPED;
            slot->play_position = 0u;
            emit_event(bank, MUSIC_BOX_EVENT_LOOP_STOPPED, index, 0u);
            break;

        case MUSIC_BOX_LOOP_EMPTY:
        case MUSIC_BOX_LOOP_PLAYING:
        case MUSIC_BOX_LOOP_STOPPED:
        case MUSIC_BOX_LOOP_ONE_SHOT:
        default:
            break;
        }
    }
}

void music_box_loop_bank_mix_sample(MusicBoxLoopBank *bank,
                                    float *left,
                                    float *right)
{
    float mixed_left = 0.0f;
    float mixed_right = 0.0f;

    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        MusicBoxLoopSlot *slot = &bank->slots[index];
        const bool one_shot = slot->state == MUSIC_BOX_LOOP_ONE_SHOT;
        const bool repeating = slot->state == MUSIC_BOX_LOOP_PLAYING ||
                               slot->state == MUSIC_BOX_LOOP_ARMED_STOP;
        if ((!one_shot && !repeating) || slot->recorded_frames == 0u) {
            continue;
        }

        mixed_left += slot->left[slot->play_position];
        mixed_right += slot->right[slot->play_position];

        slot->play_position++;
        if (slot->play_position < slot->recorded_frames) {
            continue;
        }
        slot->play_position = 0u;
        /* A sample ends at its own tail instead of wrapping like a loop. */
        if (one_shot) {
            stop_one_shot(bank, index);
        }
    }

    *left = mixed_left;
    *right = mixed_right;
}

void music_box_loop_bank_record_sample(MusicBoxLoopBank *bank,
                                       float left,
                                       float right)
{
    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        MusicBoxLoopSlot *slot = &bank->slots[index];
        if (slot->state != MUSIC_BOX_LOOP_RECORDING &&
            slot->state != MUSIC_BOX_LOOP_ARMED_FINISH) {
            continue;
        }

        if (slot->recorded_frames >= slot->capacity_frames) {
            emit_event(bank,
                       MUSIC_BOX_EVENT_LOOP_FULL,
                       index,
                       slot->recorded_frames);
            finish_recording(bank, index);
            continue;
        }

        slot->left[slot->recorded_frames] = left;
        slot->right[slot->recorded_frames] = right;
        slot->recorded_frames++;
    }
}
