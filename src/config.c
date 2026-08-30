// SPDX-License-Identifier: MIT
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return text;
    }
    char *end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return text;
}

static bool parse_unsigned(const char *text, unsigned long *value)
{
    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *trim(end) != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_float(const char *text, float *value)
{
    char *end = NULL;
    errno = 0;
    const float parsed = strtof(text, &end);
    if (errno != 0 || end == text || *trim(end) != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_bool(const char *text, bool *value)
{
    if (strcasecmp(text, "true") == 0 || strcmp(text, "1") == 0 ||
        strcasecmp(text, "yes") == 0 || strcasecmp(text, "on") == 0) {
        *value = true;
        return true;
    }
    if (strcasecmp(text, "false") == 0 || strcmp(text, "0") == 0 ||
        strcasecmp(text, "no") == 0 || strcasecmp(text, "off") == 0) {
        *value = false;
        return true;
    }
    return false;
}

static bool copy_name(char *destination, const char *source)
{
    const size_t length = strlen(source);
    if (length == 0u || length >= MUSIC_BOX_NAME_LENGTH) {
        return false;
    }
    memcpy(destination, source, length + 1u);
    return true;
}

static bool parse_loop_notes(const char *text,
                             uint8_t notes[MUSIC_BOX_LOOP_COUNT])
{
    char buffer[128];
    if (strlen(text) >= sizeof(buffer)) {
        return false;
    }
    strcpy(buffer, text);
    char *save = NULL;
    char *token = strtok_r(buffer, ",", &save);
    size_t index = 0u;
    while (token != NULL && index < MUSIC_BOX_LOOP_COUNT) {
        unsigned long note = 0u;
        if (!parse_unsigned(trim(token), &note) || note > 127u) {
            return false;
        }
        notes[index++] = (uint8_t)note;
        token = strtok_r(NULL, ",", &save);
    }
    return index == MUSIC_BOX_LOOP_COUNT && token == NULL;
}

void music_box_config_defaults(MusicBoxConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->loop_notes[0] = 52u;
    config->loop_notes[1] = 53u;
    config->loop_notes[2] = 54u;
    config->loop_notes[3] = 55u;
    config->max_loop_seconds = 60u;
    config->max_sample_seconds = 10u;
    config->pad_hold_milliseconds = 400u;
    config->mode_control_enabled = true;
    config->mode_cc = 9u;
    config->tempo_millibpm = 120000u;
    config->beats_per_bar = 4u;
    config->quantize_mode = MUSIC_BOX_QUANTIZE_BAR;
    config->display_enabled = true;
    config->display_i2c_address = 0x3cu;
    strcpy(config->display_i2c_device, "/dev/i2c-1");
    config->display_volume_seconds = 2u;
    config->volume_control_enabled = true;
    config->volume_db_curve = true;
    config->volume_cc = 3u;
    strcpy(config->volume_mixer_card, "hw:wm8960soundcard");
    strcpy(config->volume_mixer_control, "Speaker,Headphone");
    config->aec_enabled = true;
    config->aec_taps = 128u;
    config->aec_delay_frames = 512u;
    config->aec_step_size = 0.05f;
    config->aec_double_talk_ratio = 4.0f;
    config->synth_gain = 0.70f;
    config->loop_gain = 0.70f;
    config->master_gain = 0.80f;
    strcpy(config->midi_source_prefix, "MPD218");
    strcpy(config->synth_midi_prefix, "FLUID Synth");
    strcpy(config->synth_jack_prefix, "fluidsynth");
}

const char *music_box_quantize_mode_name(MusicBoxQuantizeMode mode)
{
    switch (mode) {
    case MUSIC_BOX_QUANTIZE_OFF: return "off";
    case MUSIC_BOX_QUANTIZE_BEAT: return "beat";
    case MUSIC_BOX_QUANTIZE_BAR: return "bar";
    default: return "unknown";
    }
}

const char *music_box_mode_name(MusicBoxMode mode)
{
    switch (mode) {
    case MUSIC_BOX_MODE_LOOP: return "loop";
    case MUSIC_BOX_MODE_SAMPLER: return "sampler";
    default: return "unknown";
    }
}

bool music_box_config_load(MusicBoxConfig *config, const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Cannot open configuration %s: %s\n", path, strerror(errno));
        return false;
    }
    char line[512];
    unsigned int line_number = 0u;
    bool valid = true;
    while (fgets(line, sizeof(line), file) != NULL) {
        ++line_number;
        char *text = trim(line);
        if (*text == '\0' || *text == '#' || *text == ';') continue;
        char *equals = strchr(text, '=');
        if (equals == NULL) {
            fprintf(stderr, "%s:%u: expected key=value\n", path, line_number);
            valid = false;
            break;
        }
        *equals = '\0';
        const char *key = trim(text);
        const char *value = trim(equals + 1);
        unsigned long unsigned_value = 0u;
        float float_value = 0.0f;
        if (strcmp(key, "loop_notes") == 0) {
            valid = parse_loop_notes(value, config->loop_notes);
        } else if (strcmp(key, "max_loop_seconds") == 0) {
            valid = parse_unsigned(value, &unsigned_value) && unsigned_value >= 1u && unsigned_value <= 600u;
            config->max_loop_seconds = (uint32_t)unsigned_value;
        } else if (strcmp(key, "max_sample_seconds") == 0) {
            valid = parse_unsigned(value, &unsigned_value) && unsigned_value >= 1u && unsigned_value <= 600u;
            config->max_sample_seconds = (uint32_t)unsigned_value;
        } else if (strcmp(key, "pad_hold_milliseconds") == 0) {
            valid = parse_unsigned(value, &unsigned_value) && unsigned_value >= 50u && unsigned_value <= 5000u;
            config->pad_hold_milliseconds = (uint32_t)unsigned_value;
        } else if (strcmp(key, "mode_control_enabled") == 0) {
            valid = parse_bool(value, &config->mode_control_enabled);
        } else if (strcmp(key, "mode_cc") == 0) {
            valid = parse_unsigned(value, &unsigned_value) && unsigned_value <= 127u;
            config->mode_cc = (uint8_t)unsigned_value;
        } else if (strcmp(key, "bpm") == 0) {
            valid = parse_float(value, &float_value) && float_value >= 1.0f && float_value <= 400.0f;
            config->tempo_millibpm = (uint32_t)(float_value * 1000.0f + 0.5f);
        } else if (strcmp(key, "beats_per_bar") == 0) {
            valid = parse_unsigned(value, &unsigned_value) && unsigned_value >= 1u && unsigned_value <= 16u;
            config->beats_per_bar = (uint32_t)unsigned_value;
        } else if (strcmp(key, "quantize") == 0) {
            if (strcasecmp(value, "off") == 0) config->quantize_mode = MUSIC_BOX_QUANTIZE_OFF;
            else if (strcasecmp(value, "beat") == 0) config->quantize_mode = MUSIC_BOX_QUANTIZE_BEAT;
            else if (strcasecmp(value, "bar") == 0) config->quantize_mode = MUSIC_BOX_QUANTIZE_BAR;
            else valid = false;
        } else if (strcmp(key, "aec_enabled") == 0) {
            valid = parse_bool(value, &config->aec_enabled);
        } else if (strcmp(key, "display_enabled") == 0) {
            valid = parse_bool(value, &config->display_enabled);
        } else if (strcmp(key, "display_i2c_device") == 0) {
            valid = copy_name(config->display_i2c_device, value);
        } else if (strcmp(key, "display_i2c_address") == 0) {
            char *end = NULL;
            errno = 0;
            const unsigned long address = strtoul(value, &end, 0);
            valid = errno == 0 && end != value && *trim(end) == '\0' &&
                    address >= 0x08u && address <= 0x77u;
            config->display_i2c_address = (uint8_t)address;
        } else if (strcmp(key, "display_volume_seconds") == 0) {
            valid = parse_unsigned(value, &unsigned_value) && unsigned_value <= 60u;
            config->display_volume_seconds = (uint32_t)unsigned_value;
        } else if (strcmp(key, "volume_control_enabled") == 0) {
            valid = parse_bool(value, &config->volume_control_enabled);
        } else if (strcmp(key, "volume_db_curve") == 0) {
            valid = parse_bool(value, &config->volume_db_curve);
        } else if (strcmp(key, "volume_cc") == 0) {
            valid = parse_unsigned(value, &unsigned_value) && unsigned_value <= 127u;
            config->volume_cc = (uint8_t)unsigned_value;
        } else if (strcmp(key, "volume_mixer_card") == 0) {
            valid = copy_name(config->volume_mixer_card, value);
        } else if (strcmp(key, "volume_mixer_control") == 0) {
            valid = copy_name(config->volume_mixer_control, value);
        } else if (strcmp(key, "aec_taps") == 0) {
            valid = parse_unsigned(value, &unsigned_value) && unsigned_value >= 16u && unsigned_value <= 512u;
            config->aec_taps = (size_t)unsigned_value;
        } else if (strcmp(key, "aec_delay_frames") == 0) {
            valid = parse_unsigned(value, &unsigned_value) && unsigned_value <= 8192u;
            config->aec_delay_frames = (size_t)unsigned_value;
        } else if (strcmp(key, "aec_step_size") == 0) {
            valid = parse_float(value, &config->aec_step_size) && config->aec_step_size > 0.0f && config->aec_step_size <= 1.0f;
        } else if (strcmp(key, "aec_double_talk_ratio") == 0) {
            valid = parse_float(value, &config->aec_double_talk_ratio) && config->aec_double_talk_ratio > 0.0f;
        } else if (strcmp(key, "synth_gain") == 0) {
            valid = parse_float(value, &config->synth_gain) && config->synth_gain >= 0.0f && config->synth_gain <= 2.0f;
        } else if (strcmp(key, "loop_gain") == 0) {
            valid = parse_float(value, &config->loop_gain) && config->loop_gain >= 0.0f && config->loop_gain <= 2.0f;
        } else if (strcmp(key, "master_gain") == 0) {
            valid = parse_float(value, &config->master_gain) && config->master_gain >= 0.0f && config->master_gain <= 2.0f;
        } else if (strcmp(key, "midi_source_prefix") == 0) {
            valid = copy_name(config->midi_source_prefix, value);
        } else if (strcmp(key, "synth_midi_prefix") == 0) {
            valid = copy_name(config->synth_midi_prefix, value);
        } else if (strcmp(key, "synth_jack_prefix") == 0) {
            valid = copy_name(config->synth_jack_prefix, value);
        } else {
            fprintf(stderr, "%s:%u: unknown setting '%s'\n", path, line_number, key);
            valid = false;
        }
        if (!valid) {
            fprintf(stderr, "%s:%u: invalid value for '%s'\n", path, line_number, key);
            break;
        }
    }
    if (ferror(file)) {
        fprintf(stderr, "Error reading configuration %s\n", path);
        valid = false;
    }
    fclose(file);
    return valid;
}
