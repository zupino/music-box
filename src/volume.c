// SPDX-License-Identifier: MIT
#include "volume.h"

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <string.h>

/* Used when the configured control is missing, in order of preference. */
static const char *const fallback_controls[] = {
    "Speaker", "Headphone", "PCM", "Master", "Playback",
};

uint8_t music_box_volume_percent_for_cc(uint8_t value)
{
    if (value > 127u) return 100u;
    return (uint8_t)(((unsigned int)value * 100u + 63u) / 127u);
}

long music_box_volume_raw_for_percent(long minimum, long maximum,
                                      uint8_t percent)
{
    if (maximum <= minimum) return minimum;
    if (percent > 100u) percent = 100u;
    const long range = maximum - minimum;
    return minimum + (range * (long)percent + 50L) / 100L;
}

uint8_t music_box_volume_percent_for_raw(long minimum, long maximum, long raw)
{
    if (maximum <= minimum) return 0u;
    const long range = maximum - minimum;
    long offset = raw - minimum;
    if (offset < 0L) offset = 0L;
    if (offset > range) offset = range;
    return (uint8_t)((offset * 100L + range / 2L) / range);
}

static snd_mixer_elem_t *playback_element(snd_mixer_t *mixer, const char *name)
{
    for (snd_mixer_elem_t *element = snd_mixer_first_elem(mixer);
         element != NULL; element = snd_mixer_elem_next(element)) {
        if (!snd_mixer_selem_is_active(element) ||
            !snd_mixer_selem_has_playback_volume(element))
            continue;
        const char *element_name = snd_mixer_selem_get_name(element);
        if (name == NULL) return element;
        if (element_name != NULL && strcmp(element_name, name) == 0) return element;
    }
    return NULL;
}

static snd_mixer_elem_t *select_element(snd_mixer_t *mixer, const char *preferred)
{
    snd_mixer_elem_t *element = playback_element(mixer, preferred);
    if (element != NULL) return element;
    for (size_t index = 0u;
         index < sizeof(fallback_controls) / sizeof(fallback_controls[0]);
         ++index) {
        element = playback_element(mixer, fallback_controls[index]);
        if (element != NULL) return element;
    }
    return playback_element(mixer, NULL);
}

static bool open_mixer(MusicBoxVolume *volume, const MusicBoxConfig *config)
{
    int result = snd_mixer_open(&volume->mixer, 0);
    if (result < 0) {
        fprintf(stderr, "Cannot open the ALSA mixer: %s\n", snd_strerror(result));
        volume->mixer = NULL;
        return false;
    }
    result = snd_mixer_attach(volume->mixer, config->volume_mixer_card);
    if (result < 0) {
        fprintf(stderr, "Cannot attach mixer card %s (%s); trying 'default'\n",
                config->volume_mixer_card, snd_strerror(result));
        result = snd_mixer_attach(volume->mixer, "default");
    }
    if (result < 0) {
        fprintf(stderr, "Cannot attach the default mixer card: %s\n", snd_strerror(result));
        return false;
    }
    result = snd_mixer_selem_register(volume->mixer, NULL, NULL);
    if (result < 0) {
        fprintf(stderr, "Cannot register the simple mixer: %s\n", snd_strerror(result));
        return false;
    }
    result = snd_mixer_load(volume->mixer);
    if (result < 0) {
        fprintf(stderr, "Cannot load mixer controls: %s\n", snd_strerror(result));
        return false;
    }
    volume->element = select_element(volume->mixer, config->volume_mixer_control);
    if (volume->element == NULL) {
        fprintf(stderr, "No playback volume control found on %s\n",
                config->volume_mixer_card);
        return false;
    }
    const char *name = snd_mixer_selem_get_name(volume->element);
    (void)snprintf(volume->control_name, sizeof(volume->control_name), "%s",
                   name == NULL ? "unknown" : name);
    snd_mixer_selem_get_playback_volume_range(volume->element, &volume->minimum,
                                              &volume->maximum);
    if (volume->maximum <= volume->minimum) {
        fprintf(stderr, "Mixer control %s has an empty volume range\n",
                volume->control_name);
        return false;
    }
    return true;
}

bool music_box_volume_init(MusicBoxVolume *volume, const MusicBoxConfig *config)
{
    memset(volume, 0, sizeof(*volume));
    atomic_init(&volume->percent, 0u);
    atomic_init(&volume->generation, 0u);
    volume->cc = config->volume_cc;
    if (!config->volume_control_enabled) return true;
    if (!open_mixer(volume, config)) {
        music_box_volume_destroy(volume);
        return false;
    }
    long raw = 0L;
    if (snd_mixer_selem_get_playback_volume(volume->element,
                                            SND_MIXER_SCHN_FRONT_LEFT, &raw) == 0)
        atomic_store_explicit(&volume->percent,
                              music_box_volume_percent_for_raw(volume->minimum,
                                                               volume->maximum, raw),
                              memory_order_relaxed);
    volume->available = true;
    return true;
}

void music_box_volume_destroy(MusicBoxVolume *volume)
{
    if (volume == NULL) return;
    volume->available = false;
    volume->element = NULL;
    if (volume->mixer != NULL) {
        (void)snd_mixer_close(volume->mixer);
        volume->mixer = NULL;
    }
}

bool music_box_volume_set_percent(MusicBoxVolume *volume, uint8_t percent)
{
    if (volume == NULL || !volume->available) return false;
    if (percent > 100u) percent = 100u;
    const long raw = music_box_volume_raw_for_percent(volume->minimum,
                                                      volume->maximum, percent);
    const int result = snd_mixer_selem_set_playback_volume_all(volume->element, raw);
    if (result < 0) {
        fprintf(stderr, "Cannot set %s playback volume: %s\n", volume->control_name,
                snd_strerror(result));
        return false;
    }
    atomic_store_explicit(&volume->percent, percent, memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&volume->generation, 1u, memory_order_release);
    return true;
}

bool music_box_volume_handle_cc(MusicBoxVolume *volume, uint8_t controller,
                                uint8_t value)
{
    if (volume == NULL || !volume->available || controller != volume->cc)
        return false;
    const uint8_t percent = music_box_volume_percent_for_cc(value);
    if (percent != music_box_volume_percent(volume))
        (void)music_box_volume_set_percent(volume, percent);
    return true;
}

bool music_box_volume_available(const MusicBoxVolume *volume)
{
    return volume != NULL && volume->available;
}

const char *music_box_volume_control_name(const MusicBoxVolume *volume)
{
    return volume->control_name[0] == '\0' ? "none" : volume->control_name;
}

uint8_t music_box_volume_percent(const MusicBoxVolume *volume)
{
    return (uint8_t)atomic_load_explicit(&volume->percent, memory_order_relaxed);
}

uint32_t music_box_volume_generation(const MusicBoxVolume *volume)
{
    return (uint32_t)atomic_load_explicit(&volume->generation, memory_order_acquire);
}
