// SPDX-License-Identifier: MIT
#include "session_store.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char directory[] = "/tmp/music-box-session-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    MusicBoxConfig config;
    music_box_config_defaults(&config);
    config.session_persistence_enabled = true;
    strcpy(config.session_directory, directory);
    MusicBoxSessionStore store;
    assert(music_box_session_store_init(&store, &config, 100u));

    MusicBoxLoopBank samples;
    MusicBoxLoopBank loops;
    assert(music_box_loop_bank_init(&samples, 100u, 2u, MUSIC_BOX_QUANTIZE_OFF,
                                    MUSIC_BOX_MODE_SAMPLER, 0u, NULL));
    assert(music_box_loop_bank_init(&loops, 100u, 2u, MUSIC_BOX_QUANTIZE_BAR,
                                    MUSIC_BOX_MODE_LOOP, 0u, NULL));
    samples.slots[2].recorded_frames = 3u;
    samples.slots[2].trim_start = 1u;
    samples.slots[2].trim_end = 3u;
    samples.slots[2].state = MUSIC_BOX_LOOP_STOPPED;
    samples.slots[2].left[0] = 0.1f;
    samples.slots[2].left[1] = 0.2f;
    samples.slots[2].left[2] = 0.3f;
    samples.slots[2].right[0] = -0.1f;
    samples.slots[2].right[1] = -0.2f;
    samples.slots[2].right[2] = -0.3f;
    assert(music_box_loop_bank_assign(&loops, 4u, &samples, 2u));
    assert(music_box_session_store_save_sample(&store, &samples, 2u));
    assert(music_box_session_store_save_manifest(&store, &samples, &loops));
    music_box_loop_bank_destroy(&samples);
    music_box_loop_bank_destroy(&loops);

    assert(music_box_loop_bank_init(&samples, 100u, 2u, MUSIC_BOX_QUANTIZE_OFF,
                                    MUSIC_BOX_MODE_SAMPLER, 0u, NULL));
    assert(music_box_loop_bank_init(&loops, 100u, 2u, MUSIC_BOX_QUANTIZE_BAR,
                                    MUSIC_BOX_MODE_LOOP, 0u, NULL));
    assert(music_box_session_store_load(&store, &samples, &loops, NULL));
    assert(samples.slots[2].recorded_frames == 3u);
    assert(samples.slots[2].trim_start == 1u);
    assert(samples.slots[2].trim_end == 3u);
    assert(samples.slots[2].left[1] == 0.2f);
    assert(samples.slots[2].right[2] == -0.3f);
    assert(loops.slots[4].assigned_sample == 2u);
    assert(loops.slots[4].state == MUSIC_BOX_LOOP_STOPPED);
    music_box_loop_bank_destroy(&samples);
    music_box_loop_bank_destroy(&loops);

    char command[256];
    (void)snprintf(command, sizeof(command), "rm -rf -- '%s'", directory);
    assert(system(command) == 0);
    puts("session store tests passed");
    return 0;
}
