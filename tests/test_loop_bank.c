// SPDX-License-Identifier: MIT
#include "loop_bank.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Mirrors the order the audio callback uses, mix included, because the mix is
   what tells the bank it is sounding. */
static void process_frame(MusicBoxLoopBank *bank,
                          MusicBoxTransport *transport,
                          float sample)
{
    float left = 0.0f;
    float right = 0.0f;
    music_box_loop_bank_apply_boundary(bank, transport);
    music_box_loop_bank_mix_sample(bank, &left, &right);
    music_box_loop_bank_record_sample(bank, sample, -sample);
    music_box_transport_advance(transport);
}

static void run_to_frame(MusicBoxLoopBank *bank,
                         MusicBoxTransport *transport,
                         uint64_t frame,
                         float sample)
{
    while (transport->frame_position < frame) process_frame(bank, transport, sample);
    music_box_loop_bank_apply_boundary(bank, transport);
}

/* One bar is 400 frames at 100 Hz, 60 BPM and 4 beats per bar. */
static void test_loop_mode(void)
{
    MusicBoxEventQueue events;
    music_box_event_queue_init(&events);
    MusicBoxTransport transport;
    assert(music_box_transport_init(&transport, 100u, 60000u, 4u));
    MusicBoxLoopBank loops;
    MusicBoxLoopBank samples;
    assert(music_box_loop_bank_init(&loops, 100u, 10u, MUSIC_BOX_QUANTIZE_BAR,
                                    MUSIC_BOX_MODE_LOOP, 0u, &events));
    assert(music_box_loop_bank_init(&samples, 100u, 10u, MUSIC_BOX_QUANTIZE_OFF,
                                    MUSIC_BOX_MODE_SAMPLER, 0u, &events));

    music_box_loop_bank_pad_hold(&samples, 2u, &transport);
    for (unsigned int frame = 0u; frame < 80u; ++frame)
        process_frame(&samples, &transport, 0.25f);
    music_box_loop_bank_pad_release(&samples, 2u, &transport);
    uint8_t selected = 0u;
    assert(music_box_loop_bank_select_recorded(&samples, 0u,
                                               MUSIC_BOX_LOOP_COUNT, &selected));
    assert(selected == 2u);
    assert(music_box_loop_bank_select_recorded(&samples, 15u,
                                               MUSIC_BOX_LOOP_COUNT, &selected));
    assert(selected == 2u);
    assert(music_box_loop_bank_assign(&loops, 0u, &samples, 2u));
    assert(loops.slots[0].state == MUSIC_BOX_LOOP_STOPPED);
    assert(loops.slots[0].assigned_sample == 2u);

    music_box_loop_bank_pad_press(&loops, 0u, &transport);
    assert(loops.slots[0].state == MUSIC_BOX_LOOP_ARMED_PLAY);
    run_to_frame(&loops, &transport, 400u, 0.0f);
    assert(loops.slots[0].state == MUSIC_BOX_LOOP_PLAYING);
    float left = 0.0f;
    float right = 0.0f;
    music_box_loop_bank_mix_sample(&loops, &left, &right);
    assert(fabsf(left - 0.25f) < 1.0e-6f);

    music_box_loop_bank_stop_all(&loops);
    assert(loops.slots[0].state == MUSIC_BOX_LOOP_STOPPED);

    music_box_loop_bank_set_trim(&samples, 2u, true, 64u);
    music_box_loop_bank_set_trim(&samples, 2u, false, 127u);
    assert(samples.slots[2].trim_start > 0u);
    assert(samples.slots[2].trim_end == samples.slots[2].recorded_frames);
    music_box_loop_bank_sync_assignments(&loops, &samples, 2u);
    assert(loops.slots[0].trim_start == samples.slots[2].trim_start);
    assert(loops.slots[0].trim_end == samples.slots[2].trim_end);

    music_box_loop_bank_destroy(&loops);
    music_box_loop_bank_destroy(&samples);
}

static void test_recorded_sample_selection(void)
{
    MusicBoxLoopBank samples = {0};
    uint8_t selected = 99u;
    assert(!music_box_loop_bank_select_recorded(&samples, 0u, 16u, &selected));
    samples.slots[3].recorded_frames = 10u;
    samples.slots[3].trim_end = 10u;
    samples.slots[11].recorded_frames = 20u;
    samples.slots[11].trim_start = 2u;
    samples.slots[11].trim_end = 20u;
    assert(music_box_loop_bank_select_recorded(&samples, 0u, 16u, &selected));
    assert(selected == 3u);
    assert(music_box_loop_bank_select_recorded(&samples, 7u, 16u, &selected));
    assert(selected == 3u);
    assert(music_box_loop_bank_select_recorded(&samples, 8u, 16u, &selected));
    assert(selected == 11u);
    assert(music_box_loop_bank_select_recorded(&samples, 15u, 16u, &selected));
    assert(selected == 11u);
    assert(music_box_loop_bank_select_recorded(&samples, 127u, 128u, &selected));
    assert(selected == 11u);
}

/*
 * The sampler records unquantized, so a take runs from the press to the
 * release. The bank is built with a 100 ms pre-roll, which at 100 Hz is the
 * ten frames that separate the press from the hold below.
 */
static void test_sampler_mode(void)
{
    MusicBoxEventQueue events;
    music_box_event_queue_init(&events);
    MusicBoxTransport transport;
    assert(music_box_transport_init(&transport, 100u, 60000u, 4u));
    MusicBoxLoopBank bank;
    assert(music_box_loop_bank_init(&bank, 100u, 10u, MUSIC_BOX_QUANTIZE_OFF,
                                    MUSIC_BOX_MODE_SAMPLER, 100u, &events));
    assert(bank.preroll_capacity == 10u);

    /* A press on an empty pad is a trigger, not the start of a recording. */
    music_box_loop_bank_pad_press(&bank, 1u, &transport);
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_EMPTY);
    music_box_loop_bank_pad_release(&bank, 1u, &transport);
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_EMPTY);

    /* Ten frames of the pad being down before the hold is recognised. */
    music_box_loop_bank_pad_press(&bank, 1u, &transport);
    for (unsigned int frame = 0u; frame < 10u; ++frame) process_frame(&bank, &transport, 0.25f);
    assert(bank.preroll_filled == 10u);

    music_box_loop_bank_pad_hold(&bank, 1u, &transport);
    /* No boundary is waited for, and the pre-roll is already in the slot. */
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_RECORDING);
    assert(bank.slots[1].recorded_frames == 10u);
    for (unsigned int frame = 0u; frame < 30u; ++frame) process_frame(&bank, &transport, 0.5f);

    music_box_loop_bank_pad_release(&bank, 1u, &transport);
    /* The take closes on the release and stays silent until its pad is tapped. */
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_STOPPED);
    assert(bank.slots[1].recorded_frames == 40u);
    float left = 0.0f;
    float right = 0.0f;
    music_box_loop_bank_mix_sample(&bank, &left, &right);
    assert(fabsf(left) < 1.0e-6f);

    music_box_loop_bank_pad_press(&bank, 1u, &transport);
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_ONE_SHOT);
    music_box_loop_bank_pad_release(&bank, 1u, &transport);
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_ONE_SHOT);

    /* The head is the pre-roll captured before the hold, the tail the rest. */
    music_box_loop_bank_mix_sample(&bank, &left, &right);
    assert(fabsf(left - 0.25f) < 1.0e-6f);
    assert(fabsf(right + 0.25f) < 1.0e-6f);
    for (unsigned int frame = 1u; frame < 10u; ++frame)
        music_box_loop_bank_mix_sample(&bank, &left, &right);
    assert(fabsf(left - 0.25f) < 1.0e-6f);
    for (unsigned int frame = 10u; frame < 40u; ++frame)
        music_box_loop_bank_mix_sample(&bank, &left, &right);
    assert(fabsf(left - 0.5f) < 1.0e-6f);

    /* The sample stops at its own tail instead of wrapping like a loop. */
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_STOPPED);
    music_box_loop_bank_mix_sample(&bank, &left, &right);
    assert(fabsf(left) < 1.0e-6f);

    /* Holding again replaces the sample and cancels playback in progress. */
    music_box_loop_bank_pad_press(&bank, 1u, &transport);
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_ONE_SHOT);
    music_box_loop_bank_pad_hold(&bank, 1u, &transport);
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_RECORDING);
    music_box_loop_bank_destroy(&bank);
}

/*
 * Tapping a loaded pad previews it, and that preview reaches the microphones.
 * The pre-roll must not carry it into the head of the replacement take.
 */
static void test_sampler_preroll_drops_its_own_playback(void)
{
    MusicBoxEventQueue events;
    music_box_event_queue_init(&events);
    MusicBoxTransport transport;
    assert(music_box_transport_init(&transport, 100u, 60000u, 4u));
    MusicBoxLoopBank bank;
    assert(music_box_loop_bank_init(&bank, 100u, 10u, MUSIC_BOX_QUANTIZE_OFF,
                                    MUSIC_BOX_MODE_SAMPLER, 100u, &events));

    music_box_loop_bank_pad_hold(&bank, 0u, &transport);
    for (unsigned int frame = 0u; frame < 20u; ++frame) process_frame(&bank, &transport, 0.25f);
    music_box_loop_bank_pad_release(&bank, 0u, &transport);
    assert(bank.slots[0].state == MUSIC_BOX_LOOP_STOPPED);
    assert(bank.slots[0].recorded_frames == 20u);

    /* Silent bank: the ring fills normally. */
    for (unsigned int frame = 0u; frame < 10u; ++frame) process_frame(&bank, &transport, 0.75f);
    assert(bank.preroll_filled == 10u);

    /* Previewing: every frame of history is dropped while the pad sounds. */
    music_box_loop_bank_pad_press(&bank, 0u, &transport);
    assert(bank.slots[0].state == MUSIC_BOX_LOOP_ONE_SHOT);
    process_frame(&bank, &transport, 0.75f);
    assert(bank.preroll_filled == 0u);
    for (unsigned int frame = 0u; frame < 5u; ++frame) process_frame(&bank, &transport, 0.75f);
    assert(bank.preroll_filled == 0u);

    /* So the replacement starts clean at the hold instead of with the old
       sample's bleed pasted on the front. */
    music_box_loop_bank_pad_hold(&bank, 0u, &transport);
    assert(bank.slots[0].state == MUSIC_BOX_LOOP_RECORDING);
    assert(bank.slots[0].recorded_frames == 0u);
    music_box_loop_bank_destroy(&bank);
}

static void test_monitor_recording_uses_first_empty_slot(void)
{
    MusicBoxEventQueue events;
    music_box_event_queue_init(&events);
    MusicBoxTransport transport;
    assert(music_box_transport_init(&transport, 100u, 60000u, 4u));
    MusicBoxLoopBank bank;
    assert(music_box_loop_bank_init(&bank, 100u, 10u, MUSIC_BOX_QUANTIZE_OFF,
                                    MUSIC_BOX_MODE_SAMPLER, 100u, &events));
    bank.slots[0].recorded_frames = 1u;
    bank.slots[0].trim_end = 1u;
    bank.slots[0].state = MUSIC_BOX_LOOP_STOPPED;
    bank.preroll_filled = bank.preroll_capacity;

    const int slot = music_box_loop_bank_start_monitor_recording(&bank, &transport);
    assert(slot == 1);
    assert(bank.recording_source == MUSIC_BOX_RECORD_MONITOR);
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_RECORDING);
    assert(bank.slots[1].recorded_frames == 0u);
    process_frame(&bank, &transport, 0.75f);
    assert(music_box_loop_bank_finish_recording(&bank, 1u));
    assert(bank.slots[1].state == MUSIC_BOX_LOOP_STOPPED);
    assert(bank.slots[1].recorded_frames == 1u);

    for (uint8_t index = 0u; index < MUSIC_BOX_LOOP_COUNT; ++index) {
        bank.slots[index].recorded_frames = 1u;
        bank.slots[index].state = MUSIC_BOX_LOOP_STOPPED;
    }
    assert(music_box_loop_bank_start_monitor_recording(&bank, &transport) < 0);
    music_box_loop_bank_destroy(&bank);
}

int main(void)
{
    test_loop_mode();
    test_recorded_sample_selection();
    test_sampler_mode();
    test_sampler_preroll_drops_its_own_playback();
    test_monitor_recording_uses_first_empty_slot();
    puts("loop bank tests passed");
    return 0;
}
