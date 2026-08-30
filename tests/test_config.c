// SPDX-License-Identifier: MIT
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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

    /* All sixteen MPD218 pads are mapped out of the box, bottom-left first. */
    music_box_config_defaults(&config);
    assert(music_box_config_mapped_pads(&config) == MUSIC_BOX_LOOP_COUNT);
    assert(config.loop_notes[0] == 52u);
    assert(config.loop_notes[MUSIC_BOX_LOOP_COUNT - 1u] == 67u);

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
    assert(!load("mode_cc_hysteresis=33\n", &config));

    puts("config tests passed");
    return 0;
}
