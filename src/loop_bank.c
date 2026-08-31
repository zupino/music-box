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
    emit_event(bank, MUSIC_BOX_EVENT_SLOT_FINISHED, slot_index, 0u);
}

/*
 * Lays the pre-roll ring down as the head of a new recording, oldest frame
 * first, so the take starts where the pad went down rather than where the hold
 * was recognised. Two copies at most, one per side of the ring's wrap point.
 */
static size_t copy_preroll(MusicBoxLoopBank *bank, MusicBoxLoopSlot *slot)
{
    size_t frames = bank->preroll_filled;
    if (frames == 0u) return 0u;
    if (frames > slot->capacity_frames) frames = slot->capacity_frames;

    /* The oldest of the retained frames, walking back from the write cursor. */
    const size_t start = (bank->preroll_position + bank->preroll_capacity - frames) %
                         bank->preroll_capacity;
    const size_t until_wrap = bank->preroll_capacity - start;
    const size_t first = frames < until_wrap ? frames : until_wrap;

    memcpy(slot->left, bank->preroll_left + start, first * sizeof(float));
    memcpy(slot->right, bank->preroll_right + start, first * sizeof(float));
    if (frames > first) {
        memcpy(slot->left + first, bank->preroll_left, (frames - first) * sizeof(float));
        memcpy(slot->right + first, bank->preroll_right, (frames - first) * sizeof(float));
    }
    return frames;
}

static void start_recording(MusicBoxLoopBank *bank,
                            uint8_t slot_index,
                            const MusicBoxTransport *transport)
{
    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    slot->recorded_frames = copy_preroll(bank, slot);
    slot->trim_start = 0u;
    slot->trim_end = 0u;
    slot->playback_left = slot->left;
    slot->playback_right = slot->right;
    slot->play_position = 0u;
    slot->record_start_frame = transport->frame_position;
    slot->state = MUSIC_BOX_LOOP_RECORDING;
    emit_event(bank,
               MUSIC_BOX_EVENT_RECORD_STARTED,
               slot_index,
               (uint64_t)bank->recording_source);
}

static void finish_recording(MusicBoxLoopBank *bank, uint8_t slot_index)
{
    MusicBoxLoopSlot *slot = &bank->slots[slot_index];

    slot->finish_after_start = false;
    slot->play_position = 0u;
    if (slot->recorded_frames == 0u) {
        slot->state = MUSIC_BOX_LOOP_EMPTY;
        slot->assigned_sample = UINT8_MAX;
        return;
    }
    slot->trim_end = slot->recorded_frames;

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
                              uint32_t preroll_milliseconds,
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

    if (preroll_milliseconds > 0u) {
        size_t preroll = ((size_t)sample_rate * (size_t)preroll_milliseconds) / 1000u;
        if (preroll > capacity) preroll = capacity;
        if (preroll > 0u) {
            bank->preroll_left = calloc(preroll, sizeof(float));
            bank->preroll_right = calloc(preroll, sizeof(float));
            bank->preroll_capacity = preroll;
            if (bank->preroll_left == NULL || bank->preroll_right == NULL) {
                music_box_loop_bank_destroy(bank);
                return false;
            }
        }
    }

    for (size_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        MusicBoxLoopSlot *slot = &bank->slots[index];
        /* Loop tracks reference sampler storage; only the sampler bank owns
           audio buffers now that loop mode cannot record. */
        if (mode == MUSIC_BOX_MODE_SAMPLER) {
            slot->left = calloc(capacity, sizeof(float));
            slot->right = calloc(capacity, sizeof(float));
        }
        slot->capacity_frames = capacity;
        slot->state = MUSIC_BOX_LOOP_EMPTY;
        slot->playback_left = slot->left;
        slot->playback_right = slot->right;

        if (mode == MUSIC_BOX_MODE_SAMPLER &&
            (slot->left == NULL || slot->right == NULL)) {
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

    free(bank->preroll_left);
    free(bank->preroll_right);
    bank->preroll_left = NULL;
    bank->preroll_right = NULL;
    bank->preroll_capacity = 0u;
    bank->preroll_filled = 0u;
    bank->preroll_position = 0u;
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
        slot->play_position = slot->trim_start;
        slot->state = MUSIC_BOX_LOOP_ONE_SHOT;
        emit_event(bank,
                   MUSIC_BOX_EVENT_SLOT_STARTED,
                   slot_index,
                   slot->recorded_frames);
        break;

    case MUSIC_BOX_LOOP_ONE_SHOT:
    case MUSIC_BOX_LOOP_ARMED_PLAY:
        /* Retrigger from the start; the pad already reads as playing. */
        slot->play_position = slot->trim_start;
        break;

    case MUSIC_BOX_LOOP_EMPTY:
    case MUSIC_BOX_LOOP_ARMED_RECORD:
    case MUSIC_BOX_LOOP_RECORDING:
    case MUSIC_BOX_LOOP_ARMED_FINISH:
    default:
        break;
    }
}

void music_box_loop_bank_set_recording_source(MusicBoxLoopBank *bank,
                                              MusicBoxRecordingSource source)
{
    bank->recording_source = source;
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
    (void)transport;
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
        emit_event(bank, MUSIC_BOX_EVENT_LOOP_STOP_CANCELLED, slot_index, 0u);
        break;

    case MUSIC_BOX_LOOP_EMPTY:
        break;
    case MUSIC_BOX_LOOP_STOPPED:
        if (slot->recorded_frames == 0u) break;
        if (bank->quantize_mode == MUSIC_BOX_QUANTIZE_OFF) {
            slot->state = MUSIC_BOX_LOOP_PLAYING;
            slot->play_position = slot->trim_start;
            emit_event(bank, MUSIC_BOX_EVENT_SLOT_STARTED, slot_index,
                       slot->recorded_frames);
        } else {
            slot->state = MUSIC_BOX_LOOP_ARMED_PLAY;
        }
        break;

    case MUSIC_BOX_LOOP_ARMED_RECORD:
    case MUSIC_BOX_LOOP_RECORDING:
    case MUSIC_BOX_LOOP_ARMED_FINISH:
    case MUSIC_BOX_LOOP_ONE_SHOT:
    case MUSIC_BOX_LOOP_ARMED_PLAY:
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
    const bool edit_boundary = bank->quantize_mode != MUSIC_BOX_QUANTIZE_OFF &&
        music_box_transport_is_boundary(transport, bank->quantize_mode);
    /* Assigned tracks always enter on the next beat. Record/finish/stop keeps
       the configured loop quantization for compatibility. */
    if (!transport->beat_boundary && !edit_boundary) {
        return;
    }

    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        MusicBoxLoopSlot *slot = &bank->slots[index];

        switch (slot->state) {
        case MUSIC_BOX_LOOP_ARMED_RECORD:
            if (edit_boundary) start_recording(bank, index, transport);
            break;

        case MUSIC_BOX_LOOP_ARMED_FINISH:
            if (edit_boundary && transport->frame_position > slot->record_start_frame) {
                finish_recording(bank, index);
            }
            break;

        case MUSIC_BOX_LOOP_RECORDING:
            if (edit_boundary && slot->finish_after_start &&
                transport->frame_position > slot->record_start_frame) {
                finish_recording(bank, index);
            }
            break;

        case MUSIC_BOX_LOOP_ARMED_STOP:
            if (!edit_boundary) break;
            slot->state = MUSIC_BOX_LOOP_STOPPED;
            slot->play_position = 0u;
            emit_event(bank, MUSIC_BOX_EVENT_LOOP_STOPPED, index, 0u);
            break;

        case MUSIC_BOX_LOOP_ARMED_PLAY:
            if (!transport->beat_boundary) break;
            slot->state = MUSIC_BOX_LOOP_PLAYING;
            slot->play_position = slot->trim_start;
            emit_event(bank, MUSIC_BOX_EVENT_SLOT_STARTED, index,
                       slot->recorded_frames);
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

    bank->sounding = false;
    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        MusicBoxLoopSlot *slot = &bank->slots[index];
        const bool one_shot = slot->state == MUSIC_BOX_LOOP_ONE_SHOT;
        const bool repeating = slot->state == MUSIC_BOX_LOOP_PLAYING ||
                               slot->state == MUSIC_BOX_LOOP_ARMED_STOP;
        if ((!one_shot && !repeating) || slot->recorded_frames == 0u) {
            continue;
        }

        mixed_left += slot->playback_left[slot->play_position];
        mixed_right += slot->playback_right[slot->play_position];
        bank->sounding = true;

        slot->play_position++;
        if (slot->play_position < slot->trim_end) {
            continue;
        }
        slot->play_position = slot->trim_start;
        /* A sample ends at its own tail instead of wrapping like a loop. */
        if (one_shot) {
            stop_one_shot(bank, index);
        }
    }

    *left = mixed_left;
    *right = mixed_right;
}

bool music_box_loop_bank_assign(MusicBoxLoopBank *loops, uint8_t loop_slot,
                                const MusicBoxLoopBank *samples, uint8_t sample_slot)
{
    if (loop_slot >= MUSIC_BOX_LOOP_COUNT || sample_slot >= MUSIC_BOX_LOOP_COUNT)
        return false;
    const MusicBoxLoopSlot *source = &samples->slots[sample_slot];
    if (source->recorded_frames == 0u || source->trim_end <= source->trim_start)
        return false;
    MusicBoxLoopSlot *target = &loops->slots[loop_slot];
    target->playback_left = source->playback_left;
    target->playback_right = source->playback_right;
    target->recorded_frames = source->recorded_frames;
    target->trim_start = source->trim_start;
    target->trim_end = source->trim_end;
    target->play_position = target->trim_start;
    target->assigned_sample = sample_slot;
    target->state = MUSIC_BOX_LOOP_STOPPED;
    emit_event(loops, MUSIC_BOX_EVENT_SAMPLE_ASSIGNED, loop_slot, sample_slot);
    return true;
}

bool music_box_loop_bank_select_recorded(const MusicBoxLoopBank *bank,
                                         uint8_t position,
                                         uint8_t division_count,
                                         uint8_t *slot)
{
    if (bank == NULL || slot == NULL || division_count == 0u ||
        position >= division_count)
        return false;

    uint8_t recorded_count = 0u;
    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        const MusicBoxLoopSlot *candidate = &bank->slots[index];
        if (candidate->recorded_frames > 0u &&
            candidate->trim_end > candidate->trim_start)
            recorded_count++;
    }
    if (recorded_count == 0u) return false;

    const uint8_t wanted = (uint8_t)(((unsigned int)position * recorded_count) /
                                     division_count);
    uint8_t ordinal = 0u;
    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        const MusicBoxLoopSlot *candidate = &bank->slots[index];
        if (candidate->recorded_frames == 0u ||
            candidate->trim_end <= candidate->trim_start)
            continue;
        if (ordinal == wanted) {
            *slot = index;
            return true;
        }
        ordinal++;
    }
    return false;
}

void music_box_loop_bank_stop_all(MusicBoxLoopBank *bank)
{
    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        MusicBoxLoopSlot *slot = &bank->slots[index];
        if (slot->state != MUSIC_BOX_LOOP_PLAYING &&
            slot->state != MUSIC_BOX_LOOP_ARMED_STOP &&
            slot->state != MUSIC_BOX_LOOP_ARMED_PLAY) continue;
        slot->state = MUSIC_BOX_LOOP_STOPPED;
        slot->play_position = slot->trim_start;
        emit_event(bank, MUSIC_BOX_EVENT_LOOP_STOPPED, index, 0u);
    }
}

void music_box_loop_bank_set_trim(MusicBoxLoopBank *bank, uint8_t slot_index,
                                  bool start, uint8_t value)
{
    if (slot_index >= MUSIC_BOX_LOOP_COUNT) return;
    MusicBoxLoopSlot *slot = &bank->slots[slot_index];
    if (slot->recorded_frames < 2u) return;
    const size_t position = ((size_t)value * (slot->recorded_frames - 1u)) / 127u;
    if (start) {
        slot->trim_start = position < slot->trim_end ? position : slot->trim_end - 1u;
    } else {
        const size_t end = position + 1u;
        slot->trim_end = end > slot->trim_start ? end : slot->trim_start + 1u;
    }
    if (slot->play_position < slot->trim_start || slot->play_position >= slot->trim_end)
        slot->play_position = slot->trim_start;
}

void music_box_loop_bank_sync_assignments(MusicBoxLoopBank *loops,
                                          const MusicBoxLoopBank *samples,
                                          uint8_t sample_slot)
{
    if (sample_slot >= MUSIC_BOX_LOOP_COUNT) return;
    const MusicBoxLoopSlot *source = &samples->slots[sample_slot];
    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        MusicBoxLoopSlot *target = &loops->slots[index];
        if (target->assigned_sample != sample_slot) continue;
        target->recorded_frames = source->recorded_frames;
        target->trim_start = source->trim_start;
        target->trim_end = source->trim_end;
        if (target->play_position < target->trim_start ||
            target->play_position >= target->trim_end)
            target->play_position = target->trim_start;
    }
}

static void push_preroll(MusicBoxLoopBank *bank, float left, float right)
{
    if (bank->preroll_capacity == 0u) return;
    /*
     * Tapping a loaded pad previews it, and the hold that turns the gesture
     * into a recording only arrives afterwards, so the pre-roll window is
     * exactly when the outgoing sample is loudest in the microphones. Drop the
     * history rather than prefix the new take with the old one's bleed; the
     * take then simply begins at the hold. Frames from before the press must
     * go too, since they predate the gesture.
     */
    if (bank->sounding) {
        bank->preroll_filled = 0u;
        bank->preroll_position = 0u;
        return;
    }
    bank->preroll_left[bank->preroll_position] = left;
    bank->preroll_right[bank->preroll_position] = right;
    bank->preroll_position = (bank->preroll_position + 1u) % bank->preroll_capacity;
    if (bank->preroll_filled < bank->preroll_capacity) bank->preroll_filled++;
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

    /* Written after the slots so a take started this block does not repeat the
       frame the pre-roll copy already placed at its head. */
    push_preroll(bank, left, right);
}
