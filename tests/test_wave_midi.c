// SPDX-License-Identifier: MIT
/* Exercise the actual controller dispatcher without an ALSA device. */
#include "../src/midi.c"
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
    puts("wave MIDI routing tests passed");
}
