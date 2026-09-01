// SPDX-License-Identifier: MIT
#include "audio_engine.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clamp_sample(float sample)
{
    if (sample > 1.0f) return 1.0f;
    if (sample < -1.0f) return -1.0f;
    return sample;
}

static void emit_tempo_event(MusicBoxAudioEngine *engine)
{
    const MusicBoxEvent event = {
        .type = MUSIC_BOX_EVENT_TEMPO_CHANGED,
        .slot = 0u,
        .value = engine->transport.tempo_millibpm,
    };
    (void)music_box_event_queue_push(engine->events, &event);
}

static void emit_beat_event(MusicBoxAudioEngine *engine)
{
    const MusicBoxEvent event = {
        .type = MUSIC_BOX_EVENT_BEAT,
        .slot = (uint8_t)(engine->transport.beat_index %
                          engine->transport.beats_per_bar),
        .value = engine->transport.beat_index,
    };
    (void)music_box_event_queue_push(engine->events, &event);
}

static void select_completed_monitor_recording(MusicBoxAudioEngine *engine)
{
    if (engine->monitor_recording_slot >= MUSIC_BOX_LOOP_COUNT) return;
    const uint8_t slot = engine->monitor_recording_slot;
    if (engine->sampler_bank.slots[slot].state == MUSIC_BOX_LOOP_RECORDING) return;
    engine->monitor_recording_slot = UINT8_MAX;
    if (engine->sampler_bank.slots[slot].recorded_frames == 0u) return;
    engine->selected_sample = slot;
    (void)music_box_event_queue_push(engine->events, &(MusicBoxEvent){
        .type = MUSIC_BOX_EVENT_SAMPLE_SELECTED,
        .slot = slot,
        .bank = (uint8_t)MUSIC_BOX_MODE_SAMPLER,
        .value = slot});
}

static MusicBoxLoopBank *bank_for_mode(MusicBoxAudioEngine *engine, MusicBoxMode mode)
{
    return mode == MUSIC_BOX_MODE_SAMPLER ? &engine->sampler_bank : &engine->loop_bank;
}

static void set_mode(MusicBoxAudioEngine *engine, MusicBoxMode mode)
{
    /* Nothing is stopped here: loops recorded in loop mode keep playing while
       the sampler pads are in use, and while the pads play the synthesizer. */
    if ((unsigned int)mode >= MUSIC_BOX_MODE_COUNT) return;
    if (mode == engine->mode) return;
    engine->mode = mode;
    const MusicBoxEvent event = {
        .type = MUSIC_BOX_EVENT_MODE_CHANGED,
        .slot = 0u,
        .bank = (uint8_t)mode,
        .value = (uint64_t)mode,
    };
    (void)music_box_event_queue_push(engine->events, &event);
}

/*
 * Pad mode has no bank: the MIDI thread has already forwarded the note to the
 * synthesizer, and the command reaches here only so the screen can light the
 * cell while the pad is down.
 */
static void report_pad_note(MusicBoxAudioEngine *engine,
                            const MusicBoxCommand *command)
{
    if (command->type == MUSIC_BOX_COMMAND_PAD_HOLD) return;
    const MusicBoxEvent event = {
        .type = command->type == MUSIC_BOX_COMMAND_PAD_PRESS
                    ? MUSIC_BOX_EVENT_SLOT_STARTED
                    : MUSIC_BOX_EVENT_SLOT_FINISHED,
        .slot = command->slot,
        .bank = (uint8_t)MUSIC_BOX_MODE_PAD,
        .value = 0u,
    };
    (void)music_box_event_queue_push(engine->events, &event);
}

static void route_pad_command(MusicBoxAudioEngine *engine, const MusicBoxCommand *command)
{
    if (command->slot >= MUSIC_BOX_LOOP_COUNT) return;
    /* Only a press picks the bank. The hold and release that complete the same
       gesture follow it there even if the mode knob moved in between. */
    if (command->type == MUSIC_BOX_COMMAND_PAD_PRESS)
        engine->pad_bank[command->slot] = engine->mode;
    const MusicBoxMode target = engine->pad_bank[command->slot];
    if (target == MUSIC_BOX_MODE_PAD) {
        report_pad_note(engine, command);
        return;
    }
    MusicBoxLoopBank *bank = bank_for_mode(engine, target);

    if (target == MUSIC_BOX_MODE_LOOP) {
        /* Loop gestures are resolved on hold/release. Deferring the short tap
           avoids briefly arming (or stopping) a track before the same gesture
           becomes an assignment. */
        if (command->type == MUSIC_BOX_COMMAND_PAD_PRESS) {
            engine->pad_long_gesture[command->slot] = false;
            return;
        }
        if (command->type == MUSIC_BOX_COMMAND_PAD_HOLD) {
            engine->pad_long_gesture[command->slot] = true;
            if (!music_box_loop_bank_assign(&engine->loop_bank,
                                            command->slot,
                                            &engine->sampler_bank,
                                            engine->selected_sample)) {
                (void)music_box_event_queue_push(engine->events, &(MusicBoxEvent){
                    .type = MUSIC_BOX_EVENT_SAMPLE_ASSIGN_FAILED,
                    .slot = command->slot,
                    .bank = (uint8_t)MUSIC_BOX_MODE_LOOP,
                    .value = engine->selected_sample});
            }
            return;
        }
        if (engine->pad_long_gesture[command->slot]) {
            engine->pad_long_gesture[command->slot] = false;
            return;
        }
        music_box_loop_bank_pad_press(bank, command->slot, &engine->transport);
        return;
    }

    if (target == MUSIC_BOX_MODE_SAMPLER &&
        command->type == MUSIC_BOX_COMMAND_PAD_PRESS) {
        engine->pad_long_gesture[command->slot] = false;
        /* Short taps may play samples into an active monitor take, but must not
           switch that take back to the microphone. */
        if (engine->monitor_recording_slot >= MUSIC_BOX_LOOP_COUNT) {
            engine->recording_source = (MusicBoxRecordingSource)command->recording_source;
            music_box_loop_bank_set_recording_source(&engine->sampler_bank,
                                                     engine->recording_source);
        }
    }

    switch (command->type) {
    case MUSIC_BOX_COMMAND_PAD_PRESS:
        music_box_loop_bank_pad_press(bank, command->slot, &engine->transport);
        break;
    case MUSIC_BOX_COMMAND_PAD_HOLD:
        if (target == MUSIC_BOX_MODE_SAMPLER &&
            engine->monitor_recording_slot < MUSIC_BOX_LOOP_COUNT)
            break;
        engine->pad_long_gesture[command->slot] = true;
        music_box_loop_bank_pad_hold(bank, command->slot, &engine->transport);
        break;
    case MUSIC_BOX_COMMAND_PAD_RELEASE:
    default:
        music_box_loop_bank_pad_release(bank, command->slot, &engine->transport);
        /* A completed recording becomes the trim target immediately. Tapping
           another sample for playback must not silently change the K4 target. */
        if (target == MUSIC_BOX_MODE_SAMPLER &&
            engine->pad_long_gesture[command->slot] &&
            bank->slots[command->slot].recorded_frames > 0u) {
            engine->pad_long_gesture[command->slot] = false;
            engine->selected_sample = command->slot;
            (void)music_box_event_queue_push(engine->events, &(MusicBoxEvent){
                .type = MUSIC_BOX_EVENT_SAMPLE_SELECTED,
                .slot = command->slot,
                .bank = (uint8_t)MUSIC_BOX_MODE_SAMPLER,
                .value = command->slot});
        }
        break;
    }
}

static void process_commands(MusicBoxAudioEngine *engine)
{
    MusicBoxCommand command;
    while (music_box_command_queue_pop(engine->commands, &command)) {
        switch (command.type) {
        case MUSIC_BOX_COMMAND_PAD_PRESS:
        case MUSIC_BOX_COMMAND_PAD_HOLD:
        case MUSIC_BOX_COMMAND_PAD_RELEASE:
            route_pad_command(engine, &command);
            break;
        case MUSIC_BOX_COMMAND_SET_MODE:
            set_mode(engine, (MusicBoxMode)command.mode);
            break;
        case MUSIC_BOX_COMMAND_SET_TEMPO:
            music_box_transport_set_tempo(&engine->transport, command.tempo_millibpm);
            emit_tempo_event(engine);
            break;
        case MUSIC_BOX_COMMAND_SELECT_LOOP:
            engine->selected_loop = command.value;
            (void)music_box_event_queue_push(engine->events, &(MusicBoxEvent){
                .type = MUSIC_BOX_EVENT_LOOP_SELECTED,
                .slot = engine->selected_loop,
                .bank = (uint8_t)MUSIC_BOX_MODE_LOOP,
                .value = engine->selected_loop});
            break;
        case MUSIC_BOX_COMMAND_SELECT_SAMPLE: {
            uint8_t recorded_sample = 0u;
            if (!music_box_loop_bank_select_recorded(&engine->sampler_bank,
                                                      command.value,
                                                      128u,
                                                      &recorded_sample))
                break;
            if (recorded_sample == engine->selected_sample) break;
            engine->selected_sample = recorded_sample;
            (void)music_box_event_queue_push(engine->events, &(MusicBoxEvent){
                .type = MUSIC_BOX_EVENT_SAMPLE_SELECTED,
                .slot = engine->selected_sample,
                .bank = (uint8_t)MUSIC_BOX_MODE_SAMPLER,
                .value = engine->selected_sample});
            break;
        }
        case MUSIC_BOX_COMMAND_SET_TRIM_START:
            if (engine->mode != MUSIC_BOX_MODE_SAMPLER) break;
            music_box_loop_bank_set_trim(&engine->sampler_bank,
                                         engine->selected_sample, true,
                                         command.value);
            music_box_loop_bank_sync_assignments(&engine->loop_bank,
                                                 &engine->sampler_bank,
                                                 engine->selected_sample);
            {
                const MusicBoxLoopSlot *slot =
                    &engine->sampler_bank.slots[engine->selected_sample];
                if (slot->recorded_frames > 0u) {
                    const uint64_t start = (127u * slot->trim_start) /
                                           slot->recorded_frames;
                    const uint64_t end = (127u * slot->trim_end) /
                                         slot->recorded_frames;
                    (void)music_box_event_queue_push(engine->events, &(MusicBoxEvent){
                        .type = MUSIC_BOX_EVENT_TRIM_CHANGED,
                        .slot = engine->selected_sample,
                        .bank = (uint8_t)MUSIC_BOX_MODE_SAMPLER,
                        .value = start | (end << 8u)});
                }
            }
            break;
        case MUSIC_BOX_COMMAND_SET_TRIM_END:
            if (engine->mode != MUSIC_BOX_MODE_SAMPLER) break;
            music_box_loop_bank_set_trim(&engine->sampler_bank,
                                         engine->selected_sample, false,
                                         command.value);
            music_box_loop_bank_sync_assignments(&engine->loop_bank,
                                                 &engine->sampler_bank,
                                                 engine->selected_sample);
            {
                const MusicBoxLoopSlot *slot =
                    &engine->sampler_bank.slots[engine->selected_sample];
                if (slot->recorded_frames > 0u) {
                    const uint64_t start = (127u * slot->trim_start) /
                                           slot->recorded_frames;
                    const uint64_t end = (127u * slot->trim_end) /
                                         slot->recorded_frames;
                    (void)music_box_event_queue_push(engine->events, &(MusicBoxEvent){
                        .type = MUSIC_BOX_EVENT_TRIM_CHANGED,
                        .slot = engine->selected_sample,
                        .bank = (uint8_t)MUSIC_BOX_MODE_SAMPLER,
                        .value = start | (end << 8u)});
                }
            }
            break;
        case MUSIC_BOX_COMMAND_TOGGLE_MONITOR_RECORDING:
            if (engine->monitor_recording_slot < MUSIC_BOX_LOOP_COUNT) {
                const uint8_t slot = engine->monitor_recording_slot;
                (void)music_box_loop_bank_finish_recording(&engine->sampler_bank,
                                                           slot);
                select_completed_monitor_recording(engine);
            } else {
                const int slot = music_box_loop_bank_start_monitor_recording(
                    &engine->sampler_bank, &engine->transport);
                if (slot >= 0) {
                    engine->recording_source = MUSIC_BOX_RECORD_MONITOR;
                    engine->monitor_recording_slot = (uint8_t)slot;
                } else if (slot == -1) {
                    (void)music_box_event_queue_push(engine->events, &(MusicBoxEvent){
                        .type = MUSIC_BOX_EVENT_SAMPLE_BANK_FULL,
                        .slot = 0u,
                        .bank = (uint8_t)MUSIC_BOX_MODE_SAMPLER,
                        .value = 0u});
                } else {
                    (void)music_box_event_queue_push(engine->events, &(MusicBoxEvent){
                        .type = MUSIC_BOX_EVENT_RECORDING_BUSY,
                        .slot = 0u,
                        .bank = (uint8_t)MUSIC_BOX_MODE_SAMPLER,
                        .value = 0u});
                }
            }
            break;
        case MUSIC_BOX_COMMAND_STOP_ALL_LOOPS:
            music_box_loop_bank_stop_all(&engine->loop_bank);
            break;
        default:
            break;
        }
    }
}

static int audio_process(jack_nframes_t frame_count, void *argument)
{
    MusicBoxAudioEngine *engine = argument;
    const jack_default_audio_sample_t *microphone_left = jack_port_get_buffer(engine->microphone_left, frame_count);
    const jack_default_audio_sample_t *microphone_right = jack_port_get_buffer(engine->microphone_right, frame_count);
    const jack_default_audio_sample_t *synth_left = jack_port_get_buffer(engine->synth_left, frame_count);
    const jack_default_audio_sample_t *synth_right = jack_port_get_buffer(engine->synth_right, frame_count);
    jack_default_audio_sample_t *output_left = jack_port_get_buffer(engine->output_left, frame_count);
    jack_default_audio_sample_t *output_right = jack_port_get_buffer(engine->output_right, frame_count);

    process_commands(engine);
    for (jack_nframes_t frame = 0u; frame < frame_count; ++frame) {
        if (engine->transport.beat_boundary) emit_beat_event(engine);
        music_box_loop_bank_apply_boundary(&engine->loop_bank, &engine->transport);
        music_box_loop_bank_apply_boundary(&engine->sampler_bank, &engine->transport);
        float loop_left = 0.0f;
        float loop_right = 0.0f;
        float sample_left = 0.0f;
        float sample_right = 0.0f;
        music_box_loop_bank_mix_sample(&engine->loop_bank, &loop_left, &loop_right);
        music_box_loop_bank_mix_sample(&engine->sampler_bank, &sample_left, &sample_right);
        /* Both banks play at once: switching mode never silences a running loop. */
        const float played_left = loop_left + sample_left;
        const float played_right = loop_right + sample_right;
        const float mixed_left = (synth_left[frame] * engine->config.synth_gain + played_left * engine->config.loop_gain) * engine->config.master_gain;
        const float mixed_right = (synth_right[frame] * engine->config.synth_gain + played_right * engine->config.loop_gain) * engine->config.master_gain;
        const float final_left = clamp_sample(mixed_left);
        const float final_right = clamp_sample(mixed_right);
        float clean_left = 0.0f;
        float clean_right = 0.0f;
        music_box_echo_cancel_process(&engine->echo_canceller,
                                      microphone_left[frame], microphone_right[frame],
                                      final_left, final_right,
                                      &clean_left, &clean_right);
        music_box_noise_gate_process(&engine->noise_gate, &clean_left, &clean_right);
        const float record_left = engine->recording_source == MUSIC_BOX_RECORD_MONITOR
                                      ? final_left : clean_left;
        const float record_right = engine->recording_source == MUSIC_BOX_RECORD_MONITOR
                                       ? final_right : clean_right;
        music_box_loop_bank_record_sample(&engine->sampler_bank,
                                          record_left, record_right);
        output_left[frame] = final_left;
        output_right[frame] = final_right;
        music_box_transport_advance(&engine->transport);
    }
    /* The bank also closes a take automatically at max_sample_seconds. */
    select_completed_monitor_recording(engine);
    return 0;
}

static void jack_shutdown_callback(void *argument)
{
    MusicBoxAudioEngine *engine = argument;
    atomic_store_explicit(&engine->jack_shutdown, true, memory_order_release);
}

static bool register_ports(MusicBoxAudioEngine *engine)
{
    engine->microphone_left = jack_port_register(engine->client, "mic_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0u);
    engine->microphone_right = jack_port_register(engine->client, "mic_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0u);
    engine->synth_left = jack_port_register(engine->client, "synth_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0u);
    engine->synth_right = jack_port_register(engine->client, "synth_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0u);
    engine->output_left = jack_port_register(engine->client, "out_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0u);
    engine->output_right = jack_port_register(engine->client, "out_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0u);
    return engine->microphone_left != NULL && engine->microphone_right != NULL &&
           engine->synth_left != NULL && engine->synth_right != NULL &&
           engine->output_left != NULL && engine->output_right != NULL;
}

bool music_box_audio_engine_init(MusicBoxAudioEngine *engine,
                                 const MusicBoxConfig *config,
                                 MusicBoxCommandQueue *commands,
                                 MusicBoxEventQueue *events)
{
    memset(engine, 0, sizeof(*engine));
    engine->config = *config;
    engine->commands = commands;
    engine->events = events;
    atomic_init(&engine->jack_shutdown, false);
    jack_status_t status = 0;
    engine->client = jack_client_open("music_box", JackNoStartServer, &status);
    if (engine->client == NULL) {
        fprintf(stderr, "Cannot connect to JACK (status 0x%x). Is music-box-jack running?\n", status);
        return false;
    }
    engine->mode = MUSIC_BOX_MODE_PAD;
    engine->monitor_recording_slot = UINT8_MAX;
    const uint32_t sample_rate = jack_get_sample_rate(engine->client);
    if (!music_box_transport_init(&engine->transport, sample_rate, config->tempo_millibpm, config->beats_per_bar) ||
        !music_box_echo_cancel_init(&engine->echo_canceller, config->aec_enabled, config->aec_taps,
                                    config->aec_delay_frames, config->aec_step_size, config->aec_double_talk_ratio) ||
        !music_box_noise_gate_init(&engine->noise_gate, config->noise_gate_enabled,
                                   sample_rate, config->noise_gate_threshold,
                                   config->noise_gate_floor_gain,
                                   config->noise_gate_attack_ms,
                                   config->noise_gate_release_ms)) {
        fprintf(stderr, "Cannot initialize the transport or echo canceller\n");
        music_box_audio_engine_destroy(engine);
        return false;
    }
    /* Sampler storage is reserved up front; loop tracks reference it. */
    if (!music_box_loop_bank_init(&engine->loop_bank, sample_rate, config->max_loop_seconds, config->quantize_mode,
                                  MUSIC_BOX_MODE_LOOP, 0u, events) ||
        /* A sample is never quantized: it lasts exactly as long as its pad was
           held, so it captures a spoken word or a hit rather than a bar. The
           pre-roll covers the hold that precedes the recording gesture. */
        !music_box_loop_bank_init(&engine->sampler_bank, sample_rate, config->max_sample_seconds,
                                  MUSIC_BOX_QUANTIZE_OFF, MUSIC_BOX_MODE_SAMPLER,
                                  config->pad_hold_milliseconds, events)) {
        fprintf(stderr,
                "Cannot reserve the recording buffers: %u sample slots of %u s "
                "need about %.0f MB at %u Hz. Lower max_sample_seconds in "
                "music-box.conf.\n",
                (unsigned int)MUSIC_BOX_LOOP_COUNT, (unsigned int)config->max_sample_seconds,
                (double)MUSIC_BOX_LOOP_COUNT *
                    (double)config->max_sample_seconds *
                    (double)sample_rate * 2.0 * sizeof(float) / 1.0e6,
                sample_rate);
        music_box_audio_engine_destroy(engine);
        return false;
    }
    if (!register_ports(engine)) {
        fprintf(stderr, "Cannot register JACK ports\n");
        music_box_audio_engine_destroy(engine);
        return false;
    }
    jack_set_process_callback(engine->client, audio_process, engine);
    jack_on_shutdown(engine->client, jack_shutdown_callback, engine);
    if (jack_activate(engine->client) != 0) {
        fprintf(stderr, "Cannot activate the music-box JACK client\n");
        music_box_audio_engine_destroy(engine);
        return false;
    }
    return true;
}

void music_box_audio_engine_destroy(MusicBoxAudioEngine *engine)
{
    if (engine == NULL) return;
    if (engine->client != NULL) {
        jack_deactivate(engine->client);
        jack_client_close(engine->client);
        engine->client = NULL;
    }
    music_box_echo_cancel_destroy(&engine->echo_canceller);
    music_box_loop_bank_destroy(&engine->loop_bank);
    music_box_loop_bank_destroy(&engine->sampler_bank);
}

static void connect_if_needed(jack_client_t *client, const char *source, const char *destination)
{
    if (source == NULL || destination == NULL || jack_port_by_name(client, source) == NULL || jack_port_by_name(client, destination) == NULL) return;
    const int result = jack_connect(client, source, destination);
    if (result != 0 && result != EEXIST) fprintf(stderr, "JACK cannot connect %s -> %s (%d)\n", source, destination, result);
}

static void disconnect_if_connected(jack_client_t *client, const char *source, const char *destination)
{
    jack_port_t *port = jack_port_by_name(client, source);
    if (port != NULL && jack_port_connected_to(port, destination)) (void)jack_disconnect(client, source, destination);
}

static bool starts_with_client(const char *port_name, const char *prefix)
{
    const size_t length = strlen(prefix);
    return strncmp(port_name, prefix, length) == 0 && port_name[length] == ':';
}

static void find_synth_ports(MusicBoxAudioEngine *engine, const char **left, const char **right, const char ***allocated_ports)
{
    const char **ports = jack_get_ports(engine->client, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
    *allocated_ports = ports;
    if (ports == NULL) return;
    const char *fallback[2] = {NULL, NULL};
    size_t fallback_count = 0u;
    for (size_t index = 0u; ports[index] != NULL; ++index) {
        const char *name = ports[index];
        if (!starts_with_client(name, engine->config.synth_jack_prefix)) continue;
        if (fallback_count < 2u) fallback[fallback_count++] = name;
        const char *short_name = strchr(name, ':');
        short_name = short_name == NULL ? name : short_name + 1;
        if (*left == NULL && (strstr(short_name, "l_") != NULL || strstr(short_name, "left") != NULL || strstr(short_name, "out_1") != NULL)) *left = name;
        if (*right == NULL && (strstr(short_name, "r_") != NULL || strstr(short_name, "right") != NULL || strstr(short_name, "out_2") != NULL)) *right = name;
    }
    if (*left == NULL) *left = fallback[0];
    if (*right == NULL) *right = fallback[1] != NULL ? fallback[1] : fallback[0];
}

void music_box_audio_engine_connect_ports(MusicBoxAudioEngine *engine)
{
    const char *own_mic_left = jack_port_name(engine->microphone_left);
    const char *own_mic_right = jack_port_name(engine->microphone_right);
    const char *own_synth_left = jack_port_name(engine->synth_left);
    const char *own_synth_right = jack_port_name(engine->synth_right);
    const char *own_output_left = jack_port_name(engine->output_left);
    const char *own_output_right = jack_port_name(engine->output_right);
    connect_if_needed(engine->client, "system:capture_1", own_mic_left);
    connect_if_needed(engine->client, "system:capture_2", own_mic_right);
    connect_if_needed(engine->client, own_output_left, "system:playback_1");
    connect_if_needed(engine->client, own_output_right, "system:playback_2");
    const char *synth_left = NULL;
    const char *synth_right = NULL;
    const char **allocated_ports = NULL;
    find_synth_ports(engine, &synth_left, &synth_right, &allocated_ports);
    if (synth_left != NULL) {
        disconnect_if_connected(engine->client, synth_left, "system:playback_1");
        connect_if_needed(engine->client, synth_left, own_synth_left);
    }
    if (synth_right != NULL) {
        disconnect_if_connected(engine->client, synth_right, "system:playback_2");
        connect_if_needed(engine->client, synth_right, own_synth_right);
    }
    if (allocated_ports != NULL) jack_free(allocated_ports);
}

bool music_box_audio_engine_jack_shutdown(const MusicBoxAudioEngine *engine)
{
    return atomic_load_explicit(&engine->jack_shutdown, memory_order_acquire);
}

uint32_t music_box_audio_engine_sample_rate(const MusicBoxAudioEngine *engine)
{
    return engine->transport.sample_rate;
}
