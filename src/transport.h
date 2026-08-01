// SPDX-License-Identifier: MIT
#ifndef MUSIC_BOX_TRANSPORT_H
#define MUSIC_BOX_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MUSIC_BOX_QUANTIZE_OFF = 0,
    MUSIC_BOX_QUANTIZE_BEAT,
    MUSIC_BOX_QUANTIZE_BAR
} MusicBoxQuantizeMode;

typedef struct {
    uint32_t sample_rate;
    uint32_t tempo_millibpm;
    uint32_t beats_per_bar;

    /*
     * One frame adds tempo_millibpm units. A beat is reached when the
     * accumulator crosses sample_rate * 60 * 1000. This integer formulation
     * avoids long-term floating-point drift and works for fractional BPM.
     */
    uint64_t beat_accumulator;
    uint64_t beat_threshold;
    uint64_t frame_position;
    uint64_t beat_index;

    bool beat_boundary;
    bool bar_boundary;
} MusicBoxTransport;

bool music_box_transport_init(MusicBoxTransport *transport,
                              uint32_t sample_rate,
                              uint32_t tempo_millibpm,
                              uint32_t beats_per_bar);
void music_box_transport_set_tempo(MusicBoxTransport *transport,
                                   uint32_t tempo_millibpm);
void music_box_transport_advance(MusicBoxTransport *transport);
bool music_box_transport_is_boundary(const MusicBoxTransport *transport,
                                     MusicBoxQuantizeMode mode);

#endif
