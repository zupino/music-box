// SPDX-License-Identifier: MIT
#include "melody.h"

#include <string.h>

/* PAD 9 is the original pitch. Pads 1..8 descend chromatically and pads
   10..16 ascend, giving the centered-root layout requested for performance. */
static const float pitch_ratios[MUSIC_BOX_LOOP_COUNT] = {
    0.6299605f, 0.6674199f, 0.7071068f, 0.7491535f,
    0.7937005f, 0.8408964f, 0.8908987f, 0.9438743f,
    1.0f, 1.0594631f, 1.1224620f, 1.1892071f,
    1.2599211f, 1.3348399f, 1.4142136f, 1.4983071f,
};

static void emit(MusicBoxMelody *melody, MusicBoxEventType type, uint8_t pad)
{
    if (melody->events == NULL) return;
    (void)music_box_event_queue_push(melody->events, &(MusicBoxEvent){
        .type = type, .slot = pad, .bank = (uint8_t)MUSIC_BOX_MODE_MELODY,
        .value = 0u});
}

void music_box_melody_init(MusicBoxMelody *melody, MusicBoxEventQueue *events)
{
    memset(melody, 0, sizeof(*melody));
    melody->events = events;
}

bool music_box_melody_trigger(MusicBoxMelody *melody, uint8_t pad,
                              uint8_t sample_slot,
                              const MusicBoxLoopBank *samples)
{
    if (pad >= MUSIC_BOX_LOOP_COUNT || sample_slot >= MUSIC_BOX_LOOP_COUNT)
        return false;
    const MusicBoxLoopSlot *sample = &samples->slots[sample_slot];
    if (sample->recorded_frames == 0u || sample->trim_end <= sample->trim_start)
        return false;
    melody->voices[pad] = (MusicBoxMelodyVoice){
        .active = true,
        .sample_slot = sample_slot,
        .position = (float)sample->trim_start,
        .increment = pitch_ratios[pad],
    };
    emit(melody, MUSIC_BOX_EVENT_SLOT_STARTED, pad);
    return true;
}

void music_box_melody_mix_sample(MusicBoxMelody *melody,
                                 const MusicBoxLoopBank *samples,
                                 float *left, float *right)
{
    float mixed_left = 0.0f;
    float mixed_right = 0.0f;
    for (uint8_t pad = 0u; pad < MUSIC_BOX_LOOP_COUNT; ++pad) {
        MusicBoxMelodyVoice *voice = &melody->voices[pad];
        if (!voice->active) continue;
        const MusicBoxLoopSlot *sample = &samples->slots[voice->sample_slot];
        const size_t frame = (size_t)voice->position;
        if (frame >= sample->trim_end || frame >= sample->recorded_frames) {
            voice->active = false;
            emit(melody, MUSIC_BOX_EVENT_SLOT_FINISHED, pad);
            continue;
        }
        const size_t next = frame + 1u < sample->trim_end ? frame + 1u : frame;
        const float fraction = voice->position - (float)frame;
        mixed_left += sample->playback_left[frame] +
            (sample->playback_left[next] - sample->playback_left[frame]) * fraction;
        mixed_right += sample->playback_right[frame] +
            (sample->playback_right[next] - sample->playback_right[frame]) * fraction;
        voice->position += voice->increment;
    }
    *left = mixed_left;
    *right = mixed_right;
}
