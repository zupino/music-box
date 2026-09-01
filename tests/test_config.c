// SPDX-License-Identifier: MIT
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool load(const char *body, MusicBoxConfig *config)
{
    char path[] = "/tmp/music-box-config-XXXXXX";
    const int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    FILE *file = fdopen(descriptor, "w");
    assert(file != NULL);
    fputs(body, file);
    fclose(file);
    music_box_config_defaults(config);
    const bool loaded = music_box_config_load(config, path);
    remove(path);
    return loaded;
}

int main(void)
{
    MusicBoxConfig config;

    /* Slot order follows the printed PAD 1..16 labels, not ascending notes. */
    music_box_config_defaults(&config);
    assert(music_box_config_mapped_pads(&config) == MUSIC_BOX_LOOP_COUNT);
    assert(config.loop_notes[0] == 64u);
    assert(config.loop_notes[3] == 67u);
    assert(config.loop_notes[MUSIC_BOX_LOOP_COUNT - 1u] == 55u);
    assert(config.loop_select_cc == 12u);
    assert(config.sample_select_cc == 13u);
    assert(config.trim_start_cc == 14u);
    assert(config.trim_end_cc == 15u);
    assert(config.noise_gate_enabled);
    assert(!config.display_rotate_180);
    assert(config.session_persistence_enabled);
    assert(strcmp(config.session_directory, "/var/lib/music-box/session") == 0);

    /* A configuration written when the looper had four slots still starts, and
       leaves the other twelve pads inert rather than failing to parse. */
    assert(load("loop_notes=52,53,54,55\n", &config));
    assert(music_box_config_mapped_pads(&config) == 4u);
    assert(config.loop_notes[3] == 55u);
    assert(config.loop_notes[4] == MUSIC_BOX_NOTE_UNMAPPED);

    assert(load("loop_notes=52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67\n", &config));
    assert(music_box_config_mapped_pads(&config) == MUSIC_BOX_LOOP_COUNT);

    /* Empty, out of range, and longer than the pad grid are all rejected. */
    assert(!load("loop_notes=\n", &config));
    assert(!load("loop_notes=52,999\n", &config));
    assert(!load("loop_notes=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17\n", &config));

    assert(load("mode_cc_hysteresis=0\n", &config));
    assert(config.mode_cc_hysteresis == 0u);
    /* Wider than half a zone would make the middle mode unreachable. */
    assert(!load("mode_cc_hysteresis=17\n", &config));

    assert(load("loop_select_cc=20\nsample_select_cc=21\ntrim_start_cc=22\n"
                "trim_end_cc=23\nmonitor_modifier_enabled=true\n"
                "monitor_modifier_cc=24\nloop_panic_enabled=true\nloop_panic_cc=25\n"
                "noise_gate_threshold=0.02\nnoise_gate_floor_gain=0.2\n",
                &config));
    assert(config.loop_select_cc == 20u && config.loop_panic_cc == 25u);
    assert(config.monitor_modifier_enabled && config.loop_panic_enabled);

    assert(load("display_rotate_180=true\n", &config));
    assert(config.display_rotate_180);
    assert(load("session_persistence_enabled=false\n"
                "session_directory=/tmp/music-box-session\n", &config));
    assert(!config.session_persistence_enabled);
    assert(strcmp(config.session_directory, "/tmp/music-box-session") == 0);

    /* The knob's travel is four zones: loop, sampler, pad, melody. */
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_PAD, 0u, 2u) == MUSIC_BOX_MODE_LOOP);
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_LOOP, 48u, 2u) == MUSIC_BOX_MODE_SAMPLER);
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_LOOP, 80u, 2u) == MUSIC_BOX_MODE_PAD);
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_LOOP, 127u, 2u) == MUSIC_BOX_MODE_MELODY);

    /* Each switch costs only the dead band, and inside it the mode holds. */
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_LOOP, 29u, 2u) == MUSIC_BOX_MODE_LOOP);
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_LOOP, 31u, 2u) == MUSIC_BOX_MODE_LOOP);
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_LOOP, 33u, 2u) == MUSIC_BOX_MODE_SAMPLER);
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_SAMPLER, 65u, 2u) == MUSIC_BOX_MODE_PAD);
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_PAD, 97u, 2u) == MUSIC_BOX_MODE_MELODY);

    /* Every position resolves when the dead band is off, and a switch point
       itself belongs to the lower zone. */
    for (unsigned int value = 0u; value <= 127u; ++value)
        assert(music_box_mode_for_knob(MUSIC_BOX_MODE_LOOP, (uint8_t)value, 0u) ==
               (value <= 31u ? MUSIC_BOX_MODE_LOOP
                : value <= 63u ? MUSIC_BOX_MODE_SAMPLER
                : value <= 95u ? MUSIC_BOX_MODE_PAD : MUSIC_BOX_MODE_MELODY));

    /* The middle zone survives the widest dead band the parser accepts. */
    assert(music_box_mode_for_knob(MUSIC_BOX_MODE_LOOP, 47u, 16u) == MUSIC_BOX_MODE_SAMPLER);

    puts("config tests passed");
    return 0;
}
