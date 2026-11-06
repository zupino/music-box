// SPDX-License-Identifier: MIT
#include "../src/audio_engine.c"
#include <assert.h>
int main(void)
{
    MusicBoxAudioEngine engine = {0};
    MusicBoxCommandQueue commands;
    MusicBoxEventQueue events;
    music_box_command_queue_init(&commands);
    music_box_event_queue_init(&events);
    engine.commands = &commands;
    engine.events = &events;
    engine.mode = MUSIC_BOX_MODE_WAVE;
    music_box_wave_init(&engine.wave, 48000);
    MusicBoxCommand press = {.type = MUSIC_BOX_COMMAND_PAD_PRESS, .slot = 8};
    route_pad_command(&engine, &press);
    assert(engine.wave.voices[8].held);
    set_mode(&engine, MUSIC_BOX_MODE_SAMPLER);
    MusicBoxCommand release = {.type = MUSIC_BOX_COMMAND_PAD_RELEASE, .slot = 8};
    route_pad_command(&engine, &release);
    assert(!engine.wave.voices[8].held);
    assert(engine.wave.voices[8].active); /* release tail survives mode changes */
    MusicBoxCommand parameter = {.type = MUSIC_BOX_COMMAND_SET_WAVE_PARAMETER,
        .slot = 2, .value = 90};
    assert(music_box_command_queue_push(&commands, &parameter));
    process_commands(&engine);
    assert(engine.wave.parameters[2] == 90);
    MusicBoxEvent event;
    assert(music_box_event_queue_pop(&events, &event));
    assert(event.type == MUSIC_BOX_EVENT_MODE_CHANGED);
    assert(music_box_event_queue_pop(&events, &event));
    assert(event.type == MUSIC_BOX_EVENT_WAVE_PARAMETER_CHANGED && event.value == 90);
    puts("wave engine routing tests passed");
}
