// SPDX-License-Identifier: MIT
#include "volume.h"

#include <assert.h>
#include <stdio.h>

static void test_controller_mapping(void)
{
    assert(music_box_volume_percent_for_cc(0u) == 0u);
    assert(music_box_volume_percent_for_cc(127u) == 100u);
    assert(music_box_volume_percent_for_cc(64u) == 50u);
    assert(music_box_volume_percent_for_cc(1u) == 1u);
    /* The mapping never decreases as the knob turns clockwise. */
    uint8_t previous = 0u;
    for (unsigned int value = 0u; value <= 127u; ++value) {
        const uint8_t percent = music_box_volume_percent_for_cc((uint8_t)value);
        assert(percent >= previous && percent <= 100u);
        previous = percent;
    }
}

static void test_raw_scaling(void)
{
    assert(music_box_volume_raw_for_percent(0L, 127L, 0u) == 0L);
    assert(music_box_volume_raw_for_percent(0L, 127L, 100u) == 127L);
    assert(music_box_volume_raw_for_percent(0L, 100L, 50u) == 50L);
    /* Ranges may start below zero when the driver reports hundredths of dB. */
    assert(music_box_volume_raw_for_percent(-10239L, 400L, 100u) == 400L);
    assert(music_box_volume_raw_for_percent(-10239L, 400L, 0u) == -10239L);
    /* Out-of-range percentages and empty ranges stay in bounds. */
    assert(music_box_volume_raw_for_percent(0L, 127L, 200u) == 127L);
    assert(music_box_volume_raw_for_percent(5L, 5L, 100u) == 5L);
}

static void test_raw_to_percent(void)
{
    assert(music_box_volume_percent_for_raw(0L, 127L, 0L) == 0u);
    assert(music_box_volume_percent_for_raw(0L, 127L, 127L) == 100u);
    assert(music_box_volume_percent_for_raw(0L, 100L, -5L) == 0u);
    assert(music_box_volume_percent_for_raw(0L, 100L, 500L) == 100u);
    assert(music_box_volume_percent_for_raw(7L, 7L, 7L) == 0u);
    /* A percentage survives a round trip through the driver range. */
    for (unsigned int percent = 0u; percent <= 100u; ++percent) {
        const long raw = music_box_volume_raw_for_percent(0L, 255L, (uint8_t)percent);
        const uint8_t back = music_box_volume_percent_for_raw(0L, 255L, raw);
        assert(back + 1u >= percent && back <= percent + 1u);
    }
}

int main(void)
{
    test_controller_mapping();
    test_raw_scaling();
    test_raw_to_percent();
    puts("volume tests passed");
    return 0;
}
