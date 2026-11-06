// SPDX-License-Identifier: MIT
/* Exercise the actual controller dispatcher without an ALSA device. */
#include <alsa/asoundlib.h>
#include <assert.h>
static snd_seq_event_t sent[128];
static unsigned int sent_count;
static int capture_output(snd_seq_t *sequencer, snd_seq_event_t *event)
{
    (void)sequencer;
    assert(sent_count < 128u);
    sent[sent_count++] = *event;
    return 0;
}
#define snd_seq_event_output_direct capture_output
#include "../src/midi.c"
#undef snd_seq_event_output_direct
#include <assert.h>
int main(void)
{
    MusicBoxMidi midi = {0};
    MusicBoxCommandQueue queue;
    music_box_command_queue_init(&queue);
    midi.commands = &queue;
    music_box_config_defaults(&midi.config);
    midi.mode = MUSIC_BOX_MODE_WAVE;
    uint8_t cc[] = {midi.config.loop_select_cc, midi.config.sample_select_cc,
        midi.config.trim_start_cc, midi.config.trim_end_cc};
    for (uint8_t p = 0; p < 4; ++p) {
        snd_seq_event_t event = {0};
        event.type = SND_SEQ_EVENT_CONTROLLER;
        event.data.control.param = cc[p];
        event.data.control.value = 100;
        assert(queue_control_command(&midi, &event));
        MusicBoxCommand command;
        assert(music_box_command_queue_pop(&queue, &command));
        assert(command.type == MUSIC_BOX_COMMAND_SET_WAVE_PARAMETER);
        assert(command.slot == p && command.value == 100);
        assert(!music_box_command_queue_pop(&queue, &command));
    }
    assert(midi.monitor_knob_last_milliseconds == 0);
    /* Pad SoundFont notes use melodic channel 1, and their Note Off follows
       them there even if K2 moves to wave while the pad is held. */
    midi.mode = MUSIC_BOX_MODE_PAD;
    snd_seq_event_t note = {0};
    note.type = SND_SEQ_EVENT_NOTEON;
    note.data.note.channel = 9;
    note.data.note.note = midi.config.loop_notes[8];
    note.data.note.velocity = 100;
    handle_event(&midi, NULL, 3, &note);
    assert(sent_count == 1 && sent[0].type == SND_SEQ_EVENT_NOTEON);
    assert(sent[0].data.note.channel == 0 && sent[0].data.note.velocity == 100);
    assert(sent[0].dest.client == SND_SEQ_ADDRESS_SUBSCRIBERS);
    midi.mode = MUSIC_BOX_MODE_WAVE;
    note.type = SND_SEQ_EVENT_NOTEOFF;
    note.data.note.channel = 9;
    handle_event(&midi, NULL, 3, &note);
    assert(sent_count == 2 && sent[1].type == SND_SEQ_EVENT_NOTEOFF);
    assert(sent[1].data.note.channel == 0);

    /* A wave gesture must never leak its release into FluidSynth after K2. */
    note.type = SND_SEQ_EVENT_NOTEON;
    note.data.note.velocity = 100;
    handle_event(&midi, NULL, 3, &note);
    assert(sent_count == 2);
    midi.mode = MUSIC_BOX_MODE_PAD;
    note.data.note.velocity = 0;
    handle_event(&midi, NULL, 3, &note);
    assert(sent_count == 2);

    /* K4 programs all channels without suppressing the next pad note. */
    snd_seq_event_t program = {0};
    program.type = SND_SEQ_EVENT_CONTROLLER;
    program.data.control.param = midi.config.sample_select_cc;
    program.data.control.value = 10;
    handle_event(&midi, NULL, 3, &program);
    assert(sent_count == 50);
    assert(sent[4].type == SND_SEQ_EVENT_PGMCHANGE);
    assert(sent[4].data.control.channel == 0 && sent[4].data.control.value == 10);
    note.type = SND_SEQ_EVENT_NOTEON;
    note.data.note.velocity = 100;
    handle_event(&midi, NULL, 3, &note);
    assert(sent_count == 51 && sent[50].type == SND_SEQ_EVENT_NOTEON);
    puts("wave and pad MIDI routing tests passed");
}
