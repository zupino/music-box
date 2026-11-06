// SPDX-License-Identifier: MIT
#include "wave.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static float energy(MusicBoxWave *w, unsigned int frames)
{
    float sum = 0;
    for (unsigned int i = 0; i < frames; ++i) {
        float s = music_box_wave_sample(w);
        assert(isfinite(s) && fabsf(s) <= 1.281f);
        sum += s*s;
    }
    return sum;
}
int main(void)
{
    for (unsigned int rate = 8000u; rate <= 96000u; rate *= 2u) {
        for (uint8_t shape = 0; shape < 4; ++shape) {
            MusicBoxWave w;
            music_box_wave_init(&w, rate);
            music_box_wave_set(&w, 0, (uint8_t)(shape * 32));
            for (uint8_t pad = 0; pad < 16; ++pad) music_box_wave_press(&w, pad);
            assert(fabsf(w.voices[8].increment * (float)rate - 261.625565f) < 0.001f);
            assert(w.voices[0].increment < w.voices[15].increment);
            assert(energy(&w, rate / 4) > 0.01f);
            for (uint8_t cutoff = 0; cutoff < 128; ++cutoff) {
                music_box_wave_set(&w, 1, cutoff);
                music_box_wave_set(&w, 2, (uint8_t)(127 - cutoff));
                (void)energy(&w, 100);
            }
            music_box_wave_set(&w, 3, 0);
            for (uint8_t pad = 0; pad < 16; ++pad) music_box_wave_release(&w, pad);
            (void)energy(&w, rate / 20);
            for (uint8_t pad = 0; pad < 16; ++pad) assert(!w.voices[pad].active);
            assert(energy(&w, 100) == 0.0f);
        }
    }
    MusicBoxWave open, closed;
    music_box_wave_init(&open, 48000);
    music_box_wave_init(&closed, 48000);
    music_box_wave_set(&open, 1, 127);
    music_box_wave_set(&closed, 1, 0);
    music_box_wave_press(&open, 8);
    music_box_wave_press(&closed, 8);
    (void)energy(&open, 4800); (void)energy(&closed, 4800);
    assert(energy(&open, 4800) > 100 * energy(&closed, 4800));
    music_box_wave_set(&open, 3, 127);
    music_box_wave_release(&open, 8);
    (void)energy(&open, 48000);
    assert(open.voices[8].active);
    (void)energy(&open, 192000);
    assert(!open.voices[8].active);
    puts("wave synthesis tests passed");
}
