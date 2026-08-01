// SPDX-License-Identifier: MIT
#include "transport.h"

#include <stddef.h>

#define MIN_TEMPO_MILLIBPM 1000u
#define MAX_TEMPO_MILLIBPM 400000u

static uint32_t clamp_tempo(uint32_t tempo_millibpm)
{
    if (tempo_millibpm < MIN_TEMPO_MILLIBPM) {
        return MIN_TEMPO_MILLIBPM;
    }
    if (tempo_millibpm > MAX_TEMPO_MILLIBPM) {
        return MAX_TEMPO_MILLIBPM;
    }
    return tempo_millibpm;
}

bool music_box_transport_init(MusicBoxTransport *transport,
                              uint32_t sample_rate,
                              uint32_t tempo_millibpm,
                              uint32_t beats_per_bar)
{
    if (transport == NULL || sample_rate == 0u || beats_per_bar == 0u) {
        return false;
    }

    transport->sample_rate = sample_rate;
    transport->tempo_millibpm = clamp_tempo(tempo_millibpm);
    transport->beats_per_bar = beats_per_bar;
    transport->beat_accumulator = 0u;
    transport->beat_threshold =
        (uint64_t)sample_rate * 60u * 1000u;
    transport->frame_position = 0u;
    transport->beat_index = 0u;

    /* Transport frame zero is both a beat and a bar boundary. */
    transport->beat_boundary = true;
    transport->bar_boundary = true;
    return true;
}

void music_box_transport_set_tempo(MusicBoxTransport *transport,
                                   uint32_t tempo_millibpm)
{
    if (transport == NULL) {
        return;
    }
    transport->tempo_millibpm = clamp_tempo(tempo_millibpm);
}

void music_box_transport_advance(MusicBoxTransport *transport)
{
    transport->beat_boundary = false;
    transport->bar_boundary = false;
    transport->frame_position++;
    transport->beat_accumulator += transport->tempo_millibpm;

    if (transport->beat_accumulator >= transport->beat_threshold) {
        transport->beat_accumulator -= transport->beat_threshold;
        transport->beat_index++;
        transport->beat_boundary = true;
        transport->bar_boundary =
            (transport->beat_index % transport->beats_per_bar) == 0u;
    }
}

bool music_box_transport_is_boundary(const MusicBoxTransport *transport,
                                     MusicBoxQuantizeMode mode)
{
    switch (mode) {
    case MUSIC_BOX_QUANTIZE_OFF:
        return true;
    case MUSIC_BOX_QUANTIZE_BEAT:
        return transport->beat_boundary;
    case MUSIC_BOX_QUANTIZE_BAR:
        return transport->bar_boundary;
    default:
        return false;
    }
}
