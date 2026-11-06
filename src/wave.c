// SPDX-License-Identifier: MIT
#include "wave.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846f
float music_box_wave_cutoff(uint8_t value)
{
    return 40.0f * powf(400.0f, (float)value / 127.0f);
}
float music_box_wave_release_seconds(uint8_t value)
{
    return 0.02f * powf(200.0f, (float)value / 127.0f);
}
void music_box_wave_set(MusicBoxWave *wave, uint8_t parameter, uint8_t value)
{
    if (parameter >= 4u || value > 127u) return;
    wave->parameters[parameter] = value;
    float cutoff = music_box_wave_cutoff(wave->parameters[1]);
    if (cutoff > wave->sample_rate * 0.4f) cutoff = wave->sample_rate * 0.4f;
    wave->target_g = tanf(PI * cutoff / wave->sample_rate);
    /* Q 0.5..8: bounded resonance without self-oscillation. */
    wave->target_damping = 2.0f / (1.0f + 15.0f * (float)wave->parameters[2] / 127.0f);
    wave->release_step = 1.0f / (wave->sample_rate *
        music_box_wave_release_seconds(wave->parameters[3]));
}
void music_box_wave_init(MusicBoxWave *wave, uint32_t sample_rate)
{
    memset(wave, 0, sizeof(*wave));
    wave->sample_rate = (float)(sample_rate ? sample_rate : 48000u);
    wave->parameters[1] = 100u;
    wave->parameters[3] = 45u;
    music_box_wave_set(wave, 0u, 0u);
    wave->g = wave->target_g;
    wave->damping = wave->target_damping;
}
void music_box_wave_press(MusicBoxWave *wave, uint8_t pad)
{
    if (pad >= 16u) return;
    MusicBoxWaveVoice *voice = &wave->voices[pad];
    /* PAD 9 is middle C; chromatic printed-pad order, as in melody mode. */
    voice->increment = 261.625565f * powf(2.0f, ((float)pad - 8.0f) / 12.0f) / wave->sample_rate;
    voice->active = voice->held = true;
}
void music_box_wave_release(MusicBoxWave *wave, uint8_t pad)
{
    if (pad < 16u) wave->voices[pad].held = false;
}
/* PolyBLEP softens discontinuities in square and saw oscillators. */
static float blep(float phase, float step)
{
    if (phase < step) { float x = phase / step; return x + x - x*x - 1.0f; }
    if (phase > 1.0f - step) { float x = (phase - 1.0f) / step; return x*x + x + x + 1.0f; }
    return 0.0f;
}
static float oscillator(float phase, float step, unsigned int shape)
{
    if (shape == 0u) return sinf(2.0f * PI * phase);
    if (shape == 1u) return 1.0f - 4.0f * fabsf(phase - 0.5f);
    if (shape == 2u) {
        float shifted = phase + 0.5f;
        if (shifted >= 1.0f) shifted -= 1.0f;
        return (phase < 0.5f ? 1.0f : -1.0f) + blep(phase, step) - blep(shifted, step);
    }
    return 2.0f * phase - 1.0f - blep(phase, step);
}
float music_box_wave_sample(MusicBoxWave *wave)
{
    /* Smooth filter movement (~5 ms), retaining stable TPT filter states. */
    const float smooth = 1.0f / (0.005f * wave->sample_rate);
    wave->g += smooth * (wave->target_g - wave->g);
    wave->damping += smooth * (wave->target_damping - wave->damping);
    const float g = wave->g, k = wave->damping;
    const float a = 1.0f / (1.0f + g * (g + k));
    float mixed = 0.0f;
    for (unsigned int pad = 0u; pad < 16u; ++pad) {
        MusicBoxWaveVoice *v = &wave->voices[pad];
        if (!v->active) continue;
        if (v->held) {
            v->amplitude += 1.0f / (0.005f * wave->sample_rate);
            if (v->amplitude > 1.0f) v->amplitude = 1.0f;
        } else {
            v->amplitude -= wave->release_step;
            if (v->amplitude <= 0.0f) {
                memset(v, 0, sizeof(*v));
                continue;
            }
        }
        const float input = oscillator(v->phase, v->increment, wave->parameters[0] / 32u);
        v->phase += v->increment;
        if (v->phase >= 1.0f) v->phase -= 1.0f;
        const float band = a * (v->band + g * (input - v->low));
        const float low = v->low + g * band;
        v->band = 2.0f * band - v->band;
        v->low = 2.0f * low - v->low;
        /* Resonant peaks are bounded before the shared output mixer. */
        mixed += tanhf(low * 0.5f) * v->amplitude * 0.08f;
    }
    return mixed;
}
