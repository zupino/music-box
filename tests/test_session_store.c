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
    samples.slots[0].recorded_frames = 3u;
    samples.slots[0].trim_start = 1u;
    samples.slots[0].trim_end = 3u;
    samples.slots[0].state = MUSIC_BOX_LOOP_STOPPED;
    samples.slots[0].left[0] = 0.1f;
    samples.slots[0].left[1] = 0.2f;
    samples.slots[0].left[2] = 0.3f;
    samples.slots[0].right[0] = -0.1f;
    samples.slots[0].right[1] = -0.2f;
    samples.slots[0].right[2] = -0.3f;
    assert(music_box_loop_bank_assign(&loops, 4u, &samples, 0u));
    assert(music_box_session_store_save_sample(&store, &samples, 0u));
    assert(music_box_session_store_save_manifest(&store, &samples, &loops));
    music_box_loop_bank_destroy(&samples);
    music_box_loop_bank_destroy(&loops);

    assert(music_box_loop_bank_init(&samples, 100u, 2u, MUSIC_BOX_QUANTIZE_OFF,
                                    MUSIC_BOX_MODE_SAMPLER, 0u, NULL));
    assert(music_box_loop_bank_init(&loops, 100u, 2u, MUSIC_BOX_QUANTIZE_BAR,
                                    MUSIC_BOX_MODE_LOOP, 0u, NULL));
    MusicBoxEventQueue restored_events;
    music_box_event_queue_init(&restored_events);
    assert(music_box_session_store_load(&store, &samples, &loops,
                                        &restored_events));
    assert(samples.slots[0].recorded_frames == 3u);
    assert(samples.slots[0].trim_start == 1u);
    assert(samples.slots[0].trim_end == 3u);
    assert(samples.slots[0].left[1] == 0.2f);
    assert(samples.slots[0].right[2] == -0.3f);
    assert(loops.slots[4].assigned_sample == 0u);
    assert(loops.slots[4].state == MUSIC_BOX_LOOP_STOPPED);
    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index)
        if (index != 4u) assert(loops.slots[index].assigned_sample == UINT8_MAX);
    MusicBoxEvent event;
    assert(music_box_event_queue_pop(&restored_events, &event));
    assert(event.type == MUSIC_BOX_EVENT_RESTORED_SAMPLE && event.slot == 0u);
    assert(music_box_event_queue_pop(&restored_events, &event));
    assert(event.type == MUSIC_BOX_EVENT_RESTORED_LOOP && event.slot == 4u);
    assert(!music_box_event_queue_pop(&restored_events, &event));
    music_box_loop_bank_destroy(&samples);
    music_box_loop_bank_destroy(&loops);

    /* Version 1 wrote every unassigned loop as sample zero. Treat those
       entries as ambiguous rather than restoring sixteen false assignments. */
    char manifest[256];
    (void)snprintf(manifest, sizeof(manifest), "%s/manifest", directory);
    FILE *legacy = fopen(manifest, "w");
    assert(legacy != NULL);
    assert(fputs("version 1\nrate 100\nsample 0 3 0 3\n", legacy) >= 0);
    for (unsigned int index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index)
        assert(fprintf(legacy, "loop %u 0\n", index) > 0);
    assert(fclose(legacy) == 0);
    assert(music_box_loop_bank_init(&samples, 100u, 2u, MUSIC_BOX_QUANTIZE_OFF,
                                    MUSIC_BOX_MODE_SAMPLER, 0u, NULL));
    assert(music_box_loop_bank_init(&loops, 100u, 2u, MUSIC_BOX_QUANTIZE_BAR,
                                    MUSIC_BOX_MODE_LOOP, 0u, NULL));
    assert(music_box_session_store_load(&store, &samples, &loops, NULL));
    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index)
        assert(loops.slots[index].assigned_sample == UINT8_MAX);
    music_box_loop_bank_destroy(&samples);
    music_box_loop_bank_destroy(&loops);

    char command[256];
    (void)snprintf(command, sizeof(command), "rm -rf -- '%s'", directory);
    assert(system(command) == 0);
    puts("session store tests passed");
    return 0;
}
