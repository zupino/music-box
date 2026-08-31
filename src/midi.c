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

static uint64_t monotonic_milliseconds(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000u + (uint64_t)time.tv_nsec / 1000000u;
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
    const MusicBoxCommand command = {.type = type, .slot = slot, .mode = 0u,
                                     .value = 0u,
                                     .recording_source = midi->pad_recording_source[slot],
                                     .tempo_millibpm = 0u};
    if (!music_box_command_queue_push(midi->commands, &command))
        fprintf(stderr, "MIDI command queue full; event dropped\n");
}

static bool queue_control_command(MusicBoxMidi *midi, const snd_seq_event_t *event)
{
    if (event->type != SND_SEQ_EVENT_CONTROLLER) return false;
    const unsigned int cc = event->data.control.param;
    const int raw = event->data.control.value;
    if (raw < 0 || raw > 127) return false;

    if (midi->config.monitor_modifier_enabled &&
        cc == (unsigned int)midi->config.monitor_modifier_cc) {
        midi->monitor_modifier_down = raw >= 64;
        return true;
    }
    MusicBoxCommand command = {.slot = 0u, .mode = 0u, .value = (uint8_t)raw,
                               .recording_source = 0u, .tempo_millibpm = 0u};
    unsigned int editor = 4u;
    if (cc == (unsigned int)midi->config.loop_select_cc &&
        midi->mode == MUSIC_BOX_MODE_PAD) {
        const uint64_t now = monotonic_milliseconds();
        /* An absolute knob emits a value for every small step. Treat values
           less than 200 ms apart as one movement, so a sweep toggles once
           rather than starting and stopping repeatedly. */
        if (midi->monitor_knob_last_milliseconds == 0u ||
            now - midi->monitor_knob_last_milliseconds >= 200u) {
            command.type = MUSIC_BOX_COMMAND_TOGGLE_MONITOR_RECORDING;
            if (!music_box_command_queue_push(midi->commands, &command))
                fprintf(stderr, "MIDI command queue full; monitor toggle dropped\n");
        }
        midi->monitor_knob_last_milliseconds = now;
        return true;
    }
    if (midi->config.loop_panic_enabled &&
        cc == (unsigned int)midi->config.loop_panic_cc) {
        if (raw >= 64) command.type = MUSIC_BOX_COMMAND_STOP_ALL_LOOPS;
        else return true;
    } else if (cc == (unsigned int)midi->config.loop_select_cc) {
        command.type = MUSIC_BOX_COMMAND_SELECT_LOOP;
        command.value = (uint8_t)(((unsigned int)raw * MUSIC_BOX_LOOP_COUNT) / 128u);
        editor = 0u;
    } else if (cc == (unsigned int)midi->config.sample_select_cc) {
        command.type = MUSIC_BOX_COMMAND_SELECT_SAMPLE;
        /* Keep the full absolute knob position. The audio engine knows which
           sampler slots are actually populated and maps this across that
           sparse set. Pre-quantising here made physical slot numbers leak into
           the selection and could display S13 for a recording on pad 1. */
        editor = 1u;
    } else if (cc == (unsigned int)midi->config.trim_start_cc) {
        if (midi->mode != MUSIC_BOX_MODE_SAMPLER) return true;
        command.type = MUSIC_BOX_COMMAND_SET_TRIM_START;
        editor = 2u;
    } else if (cc == (unsigned int)midi->config.trim_end_cc) {
        if (midi->mode != MUSIC_BOX_MODE_SAMPLER) return true;
        command.type = MUSIC_BOX_COMMAND_SET_TRIM_END;
        editor = 3u;
    } else {
        return false;
    }
    if (editor < 4u && midi->control_value_sent[editor] &&
        midi->last_control_value[editor] == command.value)
        return true;
    if (editor < 4u) {
        midi->last_control_value[editor] = command.value;
        midi->control_value_sent[editor] = true;
    }
    if (!music_box_command_queue_push(midi->commands, &command))
        fprintf(stderr, "MIDI command queue full; control dropped\n");
    return true;
}

/*
 * A pad that is still down after the hold threshold arms a sampler recording.
 * The gesture has to be reported while the pad is held rather than on release,
 * so it is timed here instead of waiting for the Note Off.
 */
static void check_pad_holds(MusicBoxMidi *midi, uint64_t now)
{
    for (uint8_t slot = 0u; slot < MUSIC_BOX_LOOP_COUNT; ++slot) {
        if (!midi->pad_held[slot] || midi->pad_hold_sent[slot]) continue;
        if (now - midi->pad_press_milliseconds[slot] < midi->config.pad_hold_milliseconds) continue;
        midi->pad_hold_sent[slot] = true;
        queue_pad_command(midi, MUSIC_BOX_COMMAND_PAD_HOLD, slot);
    }
}

/* The mode knob is absolute, so the mode only follows it once it is moved. The
   dead band around each switch point is the only rotation a change costs once
   the knob is near one, so it is configurable and deliberately narrow. */
static bool handle_mode_controller(MusicBoxMidi *midi, const snd_seq_event_t *event)
{
    if (event->type != SND_SEQ_EVENT_CONTROLLER || !midi->config.mode_control_enabled) return false;
    if (event->data.control.param != (unsigned int)midi->config.mode_cc) return false;
    const int position = event->data.control.value;
    if (position < 0 || position > 127) return true;
    const MusicBoxMode mode = music_box_mode_for_knob(midi->mode, (uint8_t)position,
                                                      midi->config.mode_cc_hysteresis);
    if (mode == midi->mode) return true;
    midi->mode = mode;
    const MusicBoxCommand command = {.type = MUSIC_BOX_COMMAND_SET_MODE,
                                     .slot = 0u,
                                     .mode = (uint8_t)mode,
                                     .value = 0u,
                                     .recording_source = 0u,
                                     .tempo_millibpm = 0u};
    if (!music_box_command_queue_push(midi->commands, &command))
        fprintf(stderr, "MIDI command queue full; mode change dropped\n");
    return true;
}

/* The MIDI thread owns the mixer handle; ALSA control ioctls never touch the
   JACK real-time thread. */
static bool handle_volume_controller(MusicBoxMidi *midi, const snd_seq_event_t *event)
{
    if (event->type != SND_SEQ_EVENT_CONTROLLER) return false;
    const unsigned int controller = event->data.control.param;
    const int value = event->data.control.value;
    if (controller > 127u || value < 0 || value > 127) return false;
    return music_box_volume_handle_cc(midi->volume, (uint8_t)controller, (uint8_t)value);
}

static void handle_event(MusicBoxMidi *midi, snd_seq_t *sequencer, int output_port, snd_seq_event_t *event)
{
    const bool note_event = event->type == SND_SEQ_EVENT_NOTEON || event->type == SND_SEQ_EVENT_NOTEOFF;
    int slot = -1;
    if (note_event) slot = loop_slot_for_note(midi, event->data.note.note);
    if (slot >= 0) {
        const bool pressed = event->type == SND_SEQ_EVENT_NOTEON && event->data.note.velocity > 0u;
        /* In pad mode the note belongs to the synthesizer, so the pad plays the
           SoundFont and the command below only drives the display. A Note Off
           follows its own Note On, so changing mode mid-press cannot hang a
           note on FluidSynth. */
        bool forward = midi->mode == MUSIC_BOX_MODE_PAD;
        if (pressed) {
            midi->pad_hold_sent[slot] = false;
            midi->pad_press_milliseconds[slot] = monotonic_milliseconds();
            midi->pad_forwarded[slot] = forward;
            midi->pad_recording_source[slot] = midi->monitor_modifier_down
                ? (uint8_t)MUSIC_BOX_RECORD_MONITOR
                : (uint8_t)MUSIC_BOX_RECORD_MICROPHONE;
        } else {
            forward = midi->pad_forwarded[slot];
            midi->pad_forwarded[slot] = false;
        }
        /* Only a looping or sampling pad needs its hold timed. */
        midi->pad_held[slot] = pressed && !forward;
        queue_pad_command(midi, pressed ? MUSIC_BOX_COMMAND_PAD_PRESS : MUSIC_BOX_COMMAND_PAD_RELEASE, (uint8_t)slot);
        if (!forward) return;
    }
    if (handle_mode_controller(midi, event)) return;
    if (queue_control_command(midi, event)) return;
    if (handle_volume_controller(midi, event)) return;
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
        /* Do not let a CC flood monopolise this thread: hold recognition and
           delivery of later pad gestures get a scheduling point every batch. */
        unsigned int event_count = 0u;
        while (event_count < 64u && snd_seq_event_input(sequencer, &event) >= 0) {
            handle_event(midi, sequencer, output_port, event);
            event_count++;
        }
        /* The 5 ms poll is the resolution of the hold threshold; the recording
           itself still starts on an exact transport boundary. */
        check_pad_holds(midi, monotonic_milliseconds());
        if (event_count < 64u) {
            const struct timespec pause = {.tv_sec = 0, .tv_nsec = 5000000};
            nanosleep(&pause, NULL);
        }
    }
    snd_seq_close(sequencer);
    return NULL;
}

bool music_box_midi_start(MusicBoxMidi *midi, const MusicBoxConfig *config, MusicBoxCommandQueue *commands, MusicBoxVolume *volume)
{
    memset(midi, 0, sizeof(*midi));
    midi->config = *config;
    midi->commands = commands;
    midi->volume = volume;
    midi->mode = MUSIC_BOX_MODE_PAD;
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
