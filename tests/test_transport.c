// SPDX-License-Identifier: MIT
#include "transport.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    MusicBoxTransport transport;
    assert(music_box_transport_init(&transport, 48000u, 120000u, 4u));
    assert(transport.frame_position == 0u);
    assert(transport.beat_boundary);
    assert(transport.bar_boundary);
    for (unsigned int frame = 0u; frame < 24000u; ++frame) music_box_transport_advance(&transport);
    assert(transport.frame_position == 24000u);
    assert(transport.beat_boundary);
    assert(!transport.bar_boundary);
    assert(transport.beat_index == 1u);
    for (unsigned int frame = 0u; frame < 72000u; ++frame) music_box_transport_advance(&transport);
    assert(transport.frame_position == 96000u);
    assert(transport.beat_boundary);
    assert(transport.bar_boundary);
    assert(transport.beat_index == 4u);
    puts("transport tests passed");
    return 0;
}
