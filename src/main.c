// SPDX-License-Identifier: MIT
#include "audio_engine.h"
#include "config.h"
#include "display.h"
#include "midi.h"
#include "rt_queue.h"
#include "volume.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t keep_running = 1;

static void signal_handler(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static void print_usage(const char *program)
{
    printf("Usage: %s [-c CONFIG]\n", program);
}

static void print_event(const MusicBoxEvent *event, uint32_t sample_rate)
{
    const unsigned int slot = (unsigned int)event->slot + 1u;
    static const char *const banks[MUSIC_BOX_MODE_COUNT] = {"Loop", "Sample", "Pad"};
    const char *bank = banks[event->bank < MUSIC_BOX_MODE_COUNT ? event->bank : 0u];
    switch (event->type) {
    case MUSIC_BOX_EVENT_RECORD_ARMED:
        printf("%s %u: recording armed\n", bank, slot);
        break;
    case MUSIC_BOX_EVENT_RECORD_STARTED:
        printf("%s %u: recording started at transport frame %llu\n", bank, slot, (unsigned long long)event->value);
        break;
    case MUSIC_BOX_EVENT_RECORD_FINISHED:
        printf("%s %u: recording finished (%.3f seconds)\n", bank, slot, sample_rate == 0u ? 0.0 : (double)event->value / sample_rate);
        break;
    case MUSIC_BOX_EVENT_LOOP_STOP_ARMED:
        printf("%s %u: stop armed\n", bank, slot);
        break;
    case MUSIC_BOX_EVENT_LOOP_STOP_CANCELLED:
        printf("%s %u: stop cancelled\n", bank, slot);
        break;
    case MUSIC_BOX_EVENT_LOOP_STOPPED:
        printf("%s %u: stopped\n", bank, slot);
        break;
    case MUSIC_BOX_EVENT_LOOP_FULL:
        printf("%s %u: maximum recording length reached\n", bank, slot);
        break;
    case MUSIC_BOX_EVENT_SLOT_STARTED:
        printf("%s %u: triggered\n", bank, slot);
        break;
    case MUSIC_BOX_EVENT_SLOT_FINISHED:
        printf("%s %u: finished\n", bank, slot);
        break;
    case MUSIC_BOX_EVENT_TEMPO_CHANGED:
        printf("Tempo: %.3f BPM\n", (double)event->value / 1000.0);
        break;
    case MUSIC_BOX_EVENT_MODE_CHANGED:
        printf("Mode: %s\n", music_box_mode_name((MusicBoxMode)event->value));
        break;
    case MUSIC_BOX_EVENT_SAMPLE_ASSIGNED:
        printf("Loop %u: assigned sample %llu\n", slot,
               (unsigned long long)event->value + 1u);
        break;
    case MUSIC_BOX_EVENT_SAMPLE_ASSIGN_FAILED:
        printf("Loop %u: sample %llu assignment failed (sample is empty)\n",
               slot, (unsigned long long)event->value + 1u);
        break;
    case MUSIC_BOX_EVENT_SAMPLE_TRIMMED:
        printf("Sample %u: trim %llu..%llu frames\n", slot,
               (unsigned long long)(event->value >> 32u),
               (unsigned long long)(event->value & UINT32_MAX));
        break;
    case MUSIC_BOX_EVENT_BEAT:
        break;
    default:
        break;
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *config_path = "/etc/music-box/music-box.conf";
    for (int index = 1; index < argc; ++index) {
        if ((strcmp(argv[index], "-c") == 0 || strcmp(argv[index], "--config") == 0) && index + 1 < argc) {
            config_path = argv[++index];
        } else if (strcmp(argv[index], "-h") == 0 || strcmp(argv[index], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    MusicBoxConfig config;
    music_box_config_defaults(&config);
    if (!music_box_config_load(&config, config_path)) return EXIT_FAILURE;

    MusicBoxCommandQueue commands;
    MusicBoxEventQueue events;
    music_box_command_queue_init(&commands);
    music_box_event_queue_init(&events);

    MusicBoxAudioEngine engine;
    if (!music_box_audio_engine_init(&engine, &config, &commands, &events)) return EXIT_FAILURE;
    MusicBoxDisplay display;
    if (!music_box_display_init(&display, &config))
        fprintf(stderr, "Continuing without the display\n");
    MusicBoxVolume volume;
    if (!music_box_volume_init(&volume, &config))
        fprintf(stderr, "Continuing without MIDI volume control\n");
    MusicBoxMidi midi;
    if (!music_box_midi_start(&midi, &config, &commands, &volume)) {
        music_box_volume_destroy(&volume);
        music_box_display_destroy(&display);
        music_box_audio_engine_destroy(&engine);
        return EXIT_FAILURE;
    }

    printf("Music Box running at %u Hz\n", music_box_audio_engine_sample_rate(&engine));
    printf("Quantization: %s, tempo: %.3f BPM, meter: %u/4\n",
           music_box_quantize_mode_name(config.quantize_mode),
           (double)config.tempo_millibpm / 1000.0,
           config.beats_per_bar);
    /* Stated explicitly: a blank panel is otherwise indistinguishable from a
       process that never got this far. */
    if (music_box_display_active(&display))
        printf("Display: %s at 0x%02x, ready\n", config.display_i2c_device,
               (unsigned int)config.display_i2c_address);
    else
        printf("Display: not running (%s)\n",
               config.display_enabled ? "initialization failed" : "disabled in config");
    const size_t pads = music_box_config_mapped_pads(&config);
    printf("Pads: %zu of %u mapped, hold %u ms to record a sample\n",
           pads, (unsigned int)MUSIC_BOX_LOOP_COUNT,
           (unsigned int)config.pad_hold_milliseconds);
    /* Sampler buffers are reserved up front; loop tracks reference them. */
    const double megabytes_per_second =
        (double)music_box_audio_engine_sample_rate(&engine) * 2.0 * sizeof(float) / 1.0e6;
    printf("Mode: %s at startup, reserving %.0f MB for %u sample slots of %u s\n",
           music_box_mode_name(MUSIC_BOX_MODE_PAD),
           (double)MUSIC_BOX_LOOP_COUNT * (double)config.max_sample_seconds *
               megabytes_per_second,
           (unsigned int)MUSIC_BOX_LOOP_COUNT, (unsigned int)config.max_sample_seconds);
    if (config.mode_control_enabled)
        printf("Mode knob: CC %u selects loop, sampler or pad in thirds, dead band +/-%u\n",
               (unsigned int)config.mode_cc, (unsigned int)config.mode_cc_hysteresis);
    else
        printf("Mode knob: disabled\n");
    printf("AEC: %s, taps: %zu, delay: %zu frames\n",
           config.aec_enabled ? "enabled" : "disabled",
           config.aec_taps,
           config.aec_delay_frames);
    if (music_box_volume_available(&volume))
        printf("Volume: CC %u controls mixer '%s', currently %u%%\n",
               (unsigned int)config.volume_cc,
               music_box_volume_control_summary(&volume),
               (unsigned int)music_box_volume_percent(&volume));
    else
        printf("Volume: MIDI control disabled\n");
    fflush(stdout);

    unsigned int route_counter = 0u;
    uint32_t volume_generation = music_box_volume_generation(&volume);
    while (keep_running && !music_box_audio_engine_jack_shutdown(&engine)) {
        if (route_counter == 0u) music_box_audio_engine_connect_ports(&engine);
        route_counter = (route_counter + 1u) % 10u;
        MusicBoxEvent event;
        while (music_box_event_queue_pop(&events, &event)) {
            print_event(&event, music_box_audio_engine_sample_rate(&engine));
            music_box_display_handle_event(&display, &event);
        }
        /* Knob moves are coalesced here so one screen redraw covers a sweep. */
        const uint32_t generation = music_box_volume_generation(&volume);
        if (generation != volume_generation) {
            volume_generation = generation;
            const uint8_t percent = music_box_volume_percent(&volume);
            printf("Volume: %u%%\n", (unsigned int)percent);
            fflush(stdout);
            music_box_display_show_volume(&display, percent);
        }
        music_box_display_tick(&display);
        const struct timespec pause = {.tv_sec = 0, .tv_nsec = 100000000};
        nanosleep(&pause, NULL);
    }

    music_box_midi_stop(&midi);
    music_box_volume_destroy(&volume);
    music_box_display_destroy(&display);
    music_box_audio_engine_destroy(&engine);
    return EXIT_SUCCESS;
}
