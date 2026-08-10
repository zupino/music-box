// SPDX-License-Identifier: MIT
#include "midi.h"

#include <alloca.h>
#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool has_prefix(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static int loop_slot_for_note(const MusicBoxMidi *midi, unsigned int note)
{
    for (size_t slot = 0u; slot < MUSIC_BOX_LOOP_COUNT; ++slot) {
        if (midi->config.loop_notes[slot] == note) return (int)slot;
    }
    return -1;
}

static void queue_pad_command(MusicBoxMidi *midi, MusicBoxCommandType type, uint8_t slot)
{
    const MusicBoxCommand command = {.type = type, .slot = slot, .tempo_millibpm = 0u};
    if (!music_box_command_queue_push(midi->commands, &command))
        fprintf(stderr, "MIDI command queue full; event dropped\n");
}

static void handle_event(MusicBoxMidi *midi, snd_seq_t *sequencer, int output_port, snd_seq_event_t *event)
{
    const bool note_event = event->type == SND_SEQ_EVENT_NOTEON || event->type == SND_SEQ_EVENT_NOTEOFF;
    int slot = -1;
    if (note_event) slot = loop_slot_for_note(midi, event->data.note.note);
    if (slot >= 0) {
        const bool pressed = event->type == SND_SEQ_EVENT_NOTEON && event->data.note.velocity > 0u;
        queue_pad_command(midi, pressed ? MUSIC_BOX_COMMAND_PAD_PRESS : MUSIC_BOX_COMMAND_PAD_RELEASE, (uint8_t)slot);
        return;
    }
    snd_seq_ev_set_source(event, (unsigned char)output_port);
    snd_seq_ev_set_subs(event);
    snd_seq_ev_set_direct(event);
    (void)snd_seq_event_output_direct(sequencer, event);
}

static void connect_matching_ports(snd_seq_t *sequencer, int own_client, int input_port, int output_port, const MusicBoxConfig *config)
{
    snd_seq_client_info_t *client_info = NULL;
    snd_seq_port_info_t *port_info = NULL;
    snd_seq_client_info_alloca(&client_info);
    snd_seq_port_info_alloca(&port_info);
    snd_seq_client_info_set_client(client_info, -1);
    while (snd_seq_query_next_client(sequencer, client_info) >= 0) {
        const int client = snd_seq_client_info_get_client(client_info);
        const char *client_name = snd_seq_client_info_get_name(client_info);
        if (client == own_client || client_name == NULL) continue;
        const bool is_controller = has_prefix(client_name, config->midi_source_prefix);
        const bool is_synth = has_prefix(client_name, config->synth_midi_prefix);
        if (!is_controller && !is_synth) continue;
        snd_seq_port_info_set_client(port_info, client);
        snd_seq_port_info_set_port(port_info, -1);
        while (snd_seq_query_next_port(sequencer, port_info) >= 0) {
            const int port = snd_seq_port_info_get_port(port_info);
            const unsigned int capabilities = snd_seq_port_info_get_capability(port_info);
            if (is_controller && (capabilities & SND_SEQ_PORT_CAP_READ) != 0u && (capabilities & SND_SEQ_PORT_CAP_SUBS_READ) != 0u) {
                const int result = snd_seq_connect_from(sequencer, input_port, client, port);
                if (result < 0 && result != -EBUSY)
                    fprintf(stderr, "Cannot connect MIDI controller %s %d:%d: %s\n", client_name, client, port, snd_strerror(result));
            }
            if (is_synth && (capabilities & SND_SEQ_PORT_CAP_WRITE) != 0u && (capabilities & SND_SEQ_PORT_CAP_SUBS_WRITE) != 0u) {
                const int result = snd_seq_connect_to(sequencer, output_port, client, port);
                if (result < 0 && result != -EBUSY)
                    fprintf(stderr, "Cannot connect synthesizer %s %d:%d: %s\n", client_name, client, port, snd_strerror(result));
            }
        }
    }
}

static uint64_t monotonic_milliseconds(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000u + (uint64_t)time.tv_nsec / 1000000u;
}

static void *midi_thread(void *argument)
{
    MusicBoxMidi *midi = argument;
    snd_seq_t *sequencer = NULL;
    int result = snd_seq_open(&sequencer, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (result < 0) {
        fprintf(stderr, "Cannot open ALSA sequencer: %s\n", snd_strerror(result));
        atomic_store(&midi->running, false);
        return NULL;
    }
    snd_seq_set_client_name(sequencer, "Music Box");
    const int own_client = snd_seq_client_id(sequencer);
    const int input_port = snd_seq_create_simple_port(sequencer, "Looper Control", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    const int output_port = snd_seq_create_simple_port(sequencer, "Synth Out", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (input_port < 0 || output_port < 0) {
        fprintf(stderr, "Cannot create ALSA sequencer ports\n");
        snd_seq_close(sequencer);
        atomic_store(&midi->running, false);
        return NULL;
    }
    uint64_t next_connection_scan = 0u;
    while (atomic_load_explicit(&midi->running, memory_order_acquire)) {
        const uint64_t now = monotonic_milliseconds();
        if (now >= next_connection_scan) {
            connect_matching_ports(sequencer, own_client, input_port, output_port, &midi->config);
            next_connection_scan = now + 1000u;
        }
        snd_seq_event_t *event = NULL;
        while (snd_seq_event_input(sequencer, &event) >= 0)
            handle_event(midi, sequencer, output_port, event);
        const struct timespec pause = {.tv_sec = 0, .tv_nsec = 5000000};
        nanosleep(&pause, NULL);
    }
    snd_seq_close(sequencer);
    return NULL;
}

bool music_box_midi_start(MusicBoxMidi *midi, const MusicBoxConfig *config, MusicBoxCommandQueue *commands)
{
    memset(midi, 0, sizeof(*midi));
    midi->config = *config;
    midi->commands = commands;
    atomic_init(&midi->running, true);
    if (pthread_create(&midi->thread, NULL, midi_thread, midi) != 0) {
        fprintf(stderr, "Cannot create MIDI thread\n");
        atomic_store(&midi->running, false);
        return false;
    }
    midi->thread_started = true;
    return true;
}

void music_box_midi_stop(MusicBoxMidi *midi)
{
    if (midi == NULL || !midi->thread_started) return;
    atomic_store_explicit(&midi->running, false, memory_order_release);
    pthread_join(midi->thread, NULL);
    midi->thread_started = false;
}
