// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_WAVE_H
#define MUSIC_BOX_WAVE_H
#include <stdbool.h>
#include <stdint.h>
/* Audio-thread owned, fixed storage: no allocation or I/O in synthesis. */
typedef struct {
    bool active, held;
    float phase, increment, amplitude, low, band;
} MusicBoxWaveVoice;
typedef struct {
    MusicBoxWaveVoice voices[16];
    uint8_t parameters[4]; /* shape, cutoff, resonance, release */
    float sample_rate, g, target_g, damping, target_damping, release_step;
} MusicBoxWave;
void music_box_wave_init(MusicBoxWave *wave, uint32_t sample_rate);
void music_box_wave_set(MusicBoxWave *wave, uint8_t parameter, uint8_t value);
void music_box_wave_press(MusicBoxWave *wave, uint8_t pad);
void music_box_wave_release(MusicBoxWave *wave, uint8_t pad);
float music_box_wave_sample(MusicBoxWave *wave);
float music_box_wave_cutoff(uint8_t value);
float music_box_wave_release_seconds(uint8_t value);
#endif
