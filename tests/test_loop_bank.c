// SPDX-License-Identifier: MIT
#include "loop_bank.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void process_frame(MusicBoxLoopBank *bank,
                          MusicBoxTransport *transport,
                          float sample)
{
    music_box_loop_bank_apply_boundary(bank, transport);
    music_box_loop_bank_record_sample(bank, sample, -sample);
    music_box_transport_advance(transport);
}

int main(void)
{
    MusicBoxEventQueue events;
    music_box_event_queue_init(&events);
    MusicBoxTransport transport;
    assert(music_box_transport_init(&transport, 100u, 60000u, 4u));
    MusicBoxLoopBank bank;
    assert(music_box_loop_bank_init(&bank, 100u, 10u, MUSIC_BOX_QUANTIZE_BAR, &events));
    for (unsigned int frame = 0u; frame < 10u; ++frame) process_frame(&bank, &transport, 0.25f);
    music_box_loop_bank_pad_press(&bank, 0u, &transport);
    music_box_loop_bank_pad_release(&bank, 0u, &transport);
    assert(bank.slots[0].state == MUSIC_BOX_LOOP_ARMED_RECORD);
    assert(bank.slots[0].finish_after_start);
    while (transport.frame_position < 400u) process_frame(&bank, &transport, 0.25f);
    music_box_loop_bank_apply_boundary(&bank, &transport);
    assert(bank.slots[0].state == MUSIC_BOX_LOOP_RECORDING);
    while (transport.frame_position < 800u) process_frame(&bank, &transport, 0.25f);
    music_box_loop_bank_apply_boundary(&bank, &transport);
    assert(bank.slots[0].state == MUSIC_BOX_LOOP_PLAYING);
    assert(bank.slots[0].recorded_frames == 400u);
    float left = 0.0f;
    float right = 0.0f;
    music_box_loop_bank_mix_sample(&bank, &left, &right);
    assert(fabsf(left - 0.25f) < 1.0e-6f);
    assert(fabsf(right + 0.25f) < 1.0e-6f);
    music_box_loop_bank_destroy(&bank);
    puts("loop bank tests passed");
    return 0;
}
