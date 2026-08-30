// SPDX-License-Identifier: MIT
#include "volume.h"

#include <alsa/asoundlib.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Below this width a dB range is close enough to perceptually linear that the
   plain proportional mapping is the better answer, as in alsa-utils. */
#define LINEAR_DECIBEL_LIMIT 2400L
/* Drivers report a mute floor as an absurd sentinel; clamp the curve to a
   usable window so the knob does not spend its travel below audibility. */
#define WIDEST_DECIBEL_WINDOW 10000L

/* Tried in order when the configured list resolves to nothing. */
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

static long clamped_decibel_minimum(long decibel_minimum, long decibel_maximum)
{
    const long floor_decibels = decibel_maximum - WIDEST_DECIBEL_WINDOW;
    return decibel_minimum < floor_decibels ? floor_decibels : decibel_minimum;
}

long music_box_volume_decibels_for_percent(long decibel_minimum,
                                           long decibel_maximum,
                                           uint8_t percent)
{
    if (decibel_maximum <= decibel_minimum) return decibel_maximum;
    if (percent > 100u) percent = 100u;
    const long bottom = clamped_decibel_minimum(decibel_minimum, decibel_maximum);
    const double fraction = (double)percent / 100.0;
    if (decibel_maximum - bottom <= LINEAR_DECIBEL_LIMIT)
        return bottom + lround(fraction * (double)(decibel_maximum - bottom));
    const double bottom_normalized =
        pow(10.0, (double)(bottom - decibel_maximum) / 6000.0);
    const double normalized = fraction * (1.0 - bottom_normalized) + bottom_normalized;
    return lround(6000.0 * log10(normalized)) + decibel_maximum;
}

uint8_t music_box_volume_percent_for_decibels(long decibel_minimum,
                                              long decibel_maximum,
                                              long decibels)
{
    if (decibel_maximum <= decibel_minimum) return 0u;
    const long bottom = clamped_decibel_minimum(decibel_minimum, decibel_maximum);
    if (decibels <= bottom) return 0u;
    if (decibels >= decibel_maximum) return 100u;
    double fraction;
    if (decibel_maximum - bottom <= LINEAR_DECIBEL_LIMIT) {
        fraction = (double)(decibels - bottom) / (double)(decibel_maximum - bottom);
    } else {
        const double bottom_normalized =
            pow(10.0, (double)(bottom - decibel_maximum) / 6000.0);
        const double normalized =
            pow(10.0, (double)(decibels - decibel_maximum) / 6000.0);
        fraction = (normalized - bottom_normalized) / (1.0 - bottom_normalized);
    }
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    return (uint8_t)lround(fraction * 100.0);
}

size_t music_box_volume_split_controls(const char *text,
                                       char names[][MUSIC_BOX_NAME_LENGTH],
                                       size_t limit)
{
    size_t count = 0u;
    if (text == NULL) return 0u;
    while (*text != '\0' && count < limit) {
        while (isspace((unsigned char)*text) || *text == ',') ++text;
        size_t length = 0u;
        while (text[length] != '\0' && text[length] != ',') ++length;
        size_t trimmed = length;
        while (trimmed > 0u && isspace((unsigned char)text[trimmed - 1u])) --trimmed;
        if (trimmed > 0u && trimmed < MUSIC_BOX_NAME_LENGTH) {
            memcpy(names[count], text, trimmed);
            names[count][trimmed] = '\0';
            ++count;
        } else if (trimmed >= MUSIC_BOX_NAME_LENGTH) {
            fprintf(stderr, "Mixer control name is too long; ignored\n");
        }
        text += length;
    }
    return count;
}

static snd_mixer_elem_t *playback_element(snd_mixer_t *mixer, const char *name)
{
    for (snd_mixer_elem_t *element = snd_mixer_first_elem(mixer);
         element != NULL; element = snd_mixer_elem_next(element)) {
        if (!snd_mixer_selem_is_active(element) ||
            !snd_mixer_selem_has_playback_volume(element))
            continue;
        if (name == NULL) return element;
        const char *element_name = snd_mixer_selem_get_name(element);
        if (element_name != NULL && strcmp(element_name, name) == 0) return element;
    }
    return NULL;
}

static bool append_control(MusicBoxVolume *volume, snd_mixer_elem_t *element)
{
    if (element == NULL || volume->control_count >= MUSIC_BOX_VOLUME_CONTROL_LIMIT)
        return false;
    for (size_t index = 0u; index < volume->control_count; ++index)
        if (volume->controls[index].element == element) return false;
    MusicBoxVolumeControl *control = &volume->controls[volume->control_count];
    control->element = element;
    snd_mixer_selem_get_playback_volume_range(element, &control->minimum,
                                              &control->maximum);
    if (control->maximum <= control->minimum) {
        fprintf(stderr, "Mixer control %s has an empty volume range; ignored\n",
                snd_mixer_selem_get_name(element));
        control->element = NULL;
        return false;
    }
    control->has_decibels =
        snd_mixer_selem_get_playback_dB_range(element, &control->decibel_minimum,
                                              &control->decibel_maximum) == 0 &&
        control->decibel_maximum > control->decibel_minimum;
    const char *name = snd_mixer_selem_get_name(element);
    (void)snprintf(control->name, sizeof(control->name), "%s",
                   name == NULL ? "unknown" : name);
    ++volume->control_count;
    return true;
}

static void select_controls(MusicBoxVolume *volume, const char *requested)
{
    char names[MUSIC_BOX_VOLUME_CONTROL_LIMIT][MUSIC_BOX_NAME_LENGTH];
    const size_t count = music_box_volume_split_controls(requested, names,
                                                         MUSIC_BOX_VOLUME_CONTROL_LIMIT);
    for (size_t index = 0u; index < count; ++index) {
        snd_mixer_elem_t *element = playback_element(volume->mixer, names[index]);
        if (element == NULL) {
            fprintf(stderr, "Mixer control '%s' not found; skipping it\n", names[index]);
            continue;
        }
        (void)append_control(volume, element);
    }
    if (volume->control_count > 0u) return;
    for (size_t index = 0u;
         index < sizeof(fallback_controls) / sizeof(fallback_controls[0]); ++index) {
        if (append_control(volume, playback_element(volume->mixer,
                                                    fallback_controls[index])))
            return;
    }
    (void)append_control(volume, playback_element(volume->mixer, NULL));
}

static void build_summary(MusicBoxVolume *volume)
{
    size_t used = 0u;
    volume->control_summary[0] = '\0';
    for (size_t index = 0u; index < volume->control_count; ++index) {
        const int written = snprintf(volume->control_summary + used,
                                     sizeof(volume->control_summary) - used,
                                     "%s%s", index == 0u ? "" : "+",
                                     volume->controls[index].name);
        if (written <= 0) break;
        used += (size_t)written;
        if (used >= sizeof(volume->control_summary)) break;
    }
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
        fprintf(stderr, "Cannot attach the default mixer card: %s\n",
                snd_strerror(result));
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
    select_controls(volume, config->volume_mixer_control);
    if (volume->control_count == 0u) {
        fprintf(stderr, "No playback volume control found on %s\n",
                config->volume_mixer_card);
        return false;
    }
    build_summary(volume);
    return true;
}

bool music_box_volume_init(MusicBoxVolume *volume, const MusicBoxConfig *config)
{
    memset(volume, 0, sizeof(*volume));
    atomic_init(&volume->percent, 0u);
    atomic_init(&volume->generation, 0u);
    volume->cc = config->volume_cc;
    volume->use_decibel_curve = config->volume_db_curve;
    if (!config->volume_control_enabled) return true;
    if (!open_mixer(volume, config)) {
        music_box_volume_destroy(volume);
        return false;
    }
    const MusicBoxVolumeControl *first = &volume->controls[0];
    long current = 0L;
    if (first->has_decibels && volume->use_decibel_curve) {
        if (snd_mixer_selem_get_playback_dB(first->element, SND_MIXER_SCHN_FRONT_LEFT,
                                            &current) == 0)
            atomic_store_explicit(&volume->percent,
                                  music_box_volume_percent_for_decibels(
                                      first->decibel_minimum, first->decibel_maximum,
                                      current),
                                  memory_order_relaxed);
    } else if (snd_mixer_selem_get_playback_volume(first->element,
                                                   SND_MIXER_SCHN_FRONT_LEFT,
                                                   &current) == 0) {
        atomic_store_explicit(&volume->percent,
                              music_box_volume_percent_for_raw(first->minimum,
                                                               first->maximum, current),
                              memory_order_relaxed);
    }
    volume->available = true;
    return true;
}

void music_box_volume_destroy(MusicBoxVolume *volume)
{
    if (volume == NULL) return;
    volume->available = false;
    volume->control_count = 0u;
    if (volume->mixer != NULL) {
        (void)snd_mixer_close(volume->mixer);
        volume->mixer = NULL;
    }
}

bool music_box_volume_set_percent(MusicBoxVolume *volume, uint8_t percent)
{
    if (volume == NULL || !volume->available) return false;
    if (percent > 100u) percent = 100u;
    /* alsa-lib caches element values on a long-lived handle and silently skips
       a write that matches its cache. Without draining the change events an
       external alsamixer or alsactl leaves that cache stale, and every knob
       position we had already written becomes a no-op at the hardware. */
    const int pending = snd_mixer_handle_events(volume->mixer);
    if (pending < 0)
        fprintf(stderr, "Cannot refresh mixer state: %s\n", snd_strerror(pending));
    bool applied = false;
    for (size_t index = 0u; index < volume->control_count; ++index) {
        MusicBoxVolumeControl *control = &volume->controls[index];
        int result;
        if (control->has_decibels && volume->use_decibel_curve) {
            const long decibels =
                music_box_volume_decibels_for_percent(control->decibel_minimum,
                                                      control->decibel_maximum, percent);
            result = snd_mixer_selem_set_playback_dB_all(control->element, decibels, 0);
        } else {
            const long raw = music_box_volume_raw_for_percent(control->minimum,
                                                              control->maximum, percent);
            result = snd_mixer_selem_set_playback_volume_all(control->element, raw);
        }
        if (result < 0) {
            fprintf(stderr, "Cannot set %s playback volume: %s\n", control->name,
                    snd_strerror(result));
            continue;
        }
        /* An output muted by a stored ALSA state would swallow the knob. */
        if (percent > 0u && snd_mixer_selem_has_playback_switch(control->element))
            (void)snd_mixer_selem_set_playback_switch_all(control->element, 1);
        applied = true;
    }
    if (!applied) return false;
    const bool changed = percent != music_box_volume_percent(volume);
    atomic_store_explicit(&volume->percent, percent, memory_order_relaxed);
    /* Only a real change wakes the display, so a knob sweep does not flicker. */
    if (changed)
        (void)atomic_fetch_add_explicit(&volume->generation, 1u, memory_order_release);
    return true;
}

bool music_box_volume_handle_cc(MusicBoxVolume *volume, uint8_t controller,
                                uint8_t value)
{
    if (volume == NULL || !volume->available || controller != volume->cc)
        return false;
    /* Always write the mixer rather than trusting the cached percentage: the
       hardware can be moved behind our back by alsactl or amixer, and a cache
       hit would then leave the knob unable to recover that position. */
    (void)music_box_volume_set_percent(volume, music_box_volume_percent_for_cc(value));
    return true;
}

bool music_box_volume_available(const MusicBoxVolume *volume)
{
    return volume != NULL && volume->available;
}

const char *music_box_volume_control_summary(const MusicBoxVolume *volume)
{
    return volume->control_summary[0] == '\0' ? "none" : volume->control_summary;
}

uint8_t music_box_volume_percent(const MusicBoxVolume *volume)
{
    return (uint8_t)atomic_load_explicit(&volume->percent, memory_order_relaxed);
}

uint32_t music_box_volume_generation(const MusicBoxVolume *volume)
{
    return (uint32_t)atomic_load_explicit(&volume->generation, memory_order_acquire);
}
