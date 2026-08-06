// SPDX-License-Identifier: MIT
#include "rt_queue.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    MusicBoxCommandQueue queue;
    music_box_command_queue_init(&queue);
    MusicBoxCommand output;
    assert(!music_box_command_queue_pop(&queue, &output));
    for (unsigned int index = 0u; index < MUSIC_BOX_QUEUE_CAPACITY - 1u; ++index) {
        const MusicBoxCommand command = {
            .type = index == 0u ? MUSIC_BOX_COMMAND_SET_MASTER_VOLUME
                                : MUSIC_BOX_COMMAND_PAD_PRESS,
            .slot = (uint8_t)(index % 4u),
            .midi_value = (uint8_t)(index % 128u),
            .tempo_millibpm = index,
        };
        assert(music_box_command_queue_push(&queue, &command));
    }
    const MusicBoxCommand extra = {
        .type = MUSIC_BOX_COMMAND_PAD_RELEASE,
        .slot = 0u,
        .midi_value = 0u,
        .tempo_millibpm = 0u,
    };
    assert(!music_box_command_queue_push(&queue, &extra));
    for (unsigned int index = 0u; index < MUSIC_BOX_QUEUE_CAPACITY - 1u; ++index) {
        assert(music_box_command_queue_pop(&queue, &output));
        assert(output.tempo_millibpm == index);
        assert(output.midi_value == (uint8_t)(index % 128u));
    }
    assert(!music_box_command_queue_pop(&queue, &output));
    puts("queue tests passed");
    return 0;
}
