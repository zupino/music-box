// SPDX-License-Identifier: MIT
#include "melody.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    MusicBoxLoopBank samples;
    assert(music_box_loop_bank_init(&samples, 100u, 2u, MUSIC_BOX_QUANTIZE_OFF,
                                    MUSIC_BOX_MODE_SAMPLER, 0u, NULL));
    MusicBoxLoopSlot *sample = &samples.slots[3];
    sample->recorded_frames = 100u;
    sample->trim_start = 10u;
    sample->trim_end = 90u;
    sample->state = MUSIC_BOX_LOOP_STOPPED;
    for (size_t frame = 0u; frame < sample->recorded_frames; ++frame) {
        sample->left[frame] = (float)frame / 100.0f;
        sample->right[frame] = -(float)frame / 100.0f;
    }

    MusicBoxMelody melody;
    music_box_melody_init(&melody, NULL);
    assert(music_box_melody_trigger(&melody, 8u, 3u, &samples));
    assert(fabsf(melody.voices[8].increment - 1.0f) < 1.0e-6f);
    assert(music_box_melody_trigger(&melody, 0u, 3u, &samples));
    assert(melody.voices[0].increment < 1.0f);
    assert(music_box_melody_trigger(&melody, 15u, 3u, &samples));
    assert(melody.voices[15].increment > 1.0f);

    float left = 0.0f;
    float right = 0.0f;
    music_box_melody_mix_sample(&melody, &samples, &left, &right);
    assert(left > 0.29f && left < 0.31f);
    assert(right < -0.29f && right > -0.31f);
    assert(!music_box_melody_trigger(&melody, 1u, 2u, &samples));
    music_box_loop_bank_destroy(&samples);
    puts("melody tests passed");
    return 0;
}
