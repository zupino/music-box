// SPDX-License-Identifier: MIT
#include "echo_cancel.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define SAMPLE_COUNT 50000u
#define PATH_DELAY 20u

static uint32_t random_state = 1u;

static float next_noise(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    const float normalized = (float)(random_state >> 8) / 16777216.0f;
    return normalized * 2.0f - 1.0f;
}

int main(void)
{
    MusicBoxEchoCanceller canceller;
    assert(music_box_echo_cancel_init(&canceller, true, 32u, PATH_DELAY, 0.08f, 10.0f));
    float reference[SAMPLE_COUNT];
    for (size_t index = 0u; index < SAMPLE_COUNT; ++index) reference[index] = next_noise() * 0.4f;
    double early_error = 0.0;
    double late_error = 0.0;
    for (size_t index = 0u; index < SAMPLE_COUNT; ++index) {
        float microphone = 0.0f;
        if (index >= PATH_DELAY) microphone += 0.55f * reference[index - PATH_DELAY];
        if (index >= PATH_DELAY + 3u) microphone += 0.20f * reference[index - PATH_DELAY - 3u];
        float clean_left = 0.0f;
        float clean_right = 0.0f;
        music_box_echo_cancel_process(&canceller, microphone, 0.0f, reference[index], 0.0f, &clean_left, &clean_right);
        if (index >= 1000u && index < 6000u) early_error += (double)clean_left * clean_left;
        if (index >= SAMPLE_COUNT - 5000u) late_error += (double)clean_left * clean_left;
    }
    music_box_echo_cancel_destroy(&canceller);
    assert(late_error < early_error * 0.20);
    puts("echo cancellation tests passed");
    return 0;
}
