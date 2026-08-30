// SPDX-License-Identifier: MIT
#include "display.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* Only the glyphs used by the compact status screen are stored. */
typedef struct {
    char character;
    uint8_t columns[5];
} Glyph;

static const Glyph font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'0', {0x3e, 0x51, 0x49, 0x45, 0x3e}},
    {'1', {0x00, 0x42, 0x7f, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4b, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7f, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3c, 0x4a, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1e}},
    {'A', {0x7e, 0x11, 0x11, 0x11, 0x7e}},
    {'B', {0x7f, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3e, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7f, 0x41, 0x41, 0x22, 0x1c}},
    {'E', {0x7f, 0x49, 0x49, 0x49, 0x41}},
    {'L', {0x7f, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7f, 0x02, 0x0c, 0x02, 0x7f}},
    {'O', {0x3e, 0x41, 0x41, 0x41, 0x3e}},
    {'P', {0x7f, 0x09, 0x09, 0x09, 0x06}},
    {'R', {0x7f, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'U', {0x3f, 0x40, 0x40, 0x40, 0x3f}},
    {'V', {0x1f, 0x20, 0x40, 0x20, 0x1f}},
    {'%', {0x63, 0x13, 0x08, 0x64, 0x63}},
};

static void set_pixel(MusicBoxDisplay *display, unsigned int x,
                      unsigned int y)
{
    if (x >= MUSIC_BOX_DISPLAY_WIDTH || y >= MUSIC_BOX_DISPLAY_HEIGHT) return;
    display->pixels[x + (y / 8u) * MUSIC_BOX_DISPLAY_WIDTH] |=
        (uint8_t)(1u << (y % 8u));
}

static const uint8_t *glyph_for(char character)
{
    for (size_t index = 0u; index < sizeof(font) / sizeof(font[0]); ++index)
        if (font[index].character == character) return font[index].columns;
    return font[0].columns;
}

static void draw_text(MusicBoxDisplay *display, unsigned int x,
                      unsigned int y, const char *text)
{
    while (*text != '\0') {
        const uint8_t *glyph = glyph_for(*text++);
        for (unsigned int column = 0u; column < 5u; ++column)
            for (unsigned int row = 0u; row < 7u; ++row)
                if ((glyph[column] & (1u << row)) != 0u)
                    set_pixel(display, x + column, y + row);
        x += 6u;
    }
}

static void draw_text_right(MusicBoxDisplay *display, unsigned int right_x,
                            unsigned int y, const char *text)
{
    const unsigned int width = (unsigned int)strlen(text) * 6u;
    draw_text(display, right_x > width ? right_x - width : 0u, y, text);
}

static void fill_rectangle(MusicBoxDisplay *display, unsigned int x,
                           unsigned int y, unsigned int width,
                           unsigned int height)
{
    for (unsigned int column = 0u; column < width; ++column)
        for (unsigned int row = 0u; row < height; ++row)
            set_pixel(display, x + column, y + row);
}

static void draw_rectangle(MusicBoxDisplay *display, unsigned int x,
                           unsigned int y, unsigned int width,
                           unsigned int height)
{
    if (width == 0u || height == 0u) return;
    fill_rectangle(display, x, y, width, 1u);
    fill_rectangle(display, x, y + height - 1u, width, 1u);
    fill_rectangle(display, x, y, 1u, height);
    fill_rectangle(display, x + width - 1u, y, 1u, height);
}

static uint64_t monotonic_milliseconds(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000u + (uint64_t)time.tv_nsec / 1000000u;
}

static bool send_bytes(int fd, uint8_t control, const uint8_t *bytes,
                       size_t count)
{
    uint8_t packet[17];
    packet[0] = control;
    while (count > 0u) {
        const size_t chunk = count > sizeof(packet) - 1u ? sizeof(packet) - 1u : count;
        memcpy(packet + 1u, bytes, chunk);
        if (write(fd, packet, chunk + 1u) != (ssize_t)(chunk + 1u)) return false;
        bytes += chunk;
        count -= chunk;
    }
    return true;
}

/*
 * Screen layout, 128x32:
 *   rows 0-6    mode name, and either the tempo or the recording state;
 *   rows 7-25   a 4x4 grid mirroring the MPD218 pads;
 *   rows 28-31  the beat footer, present on every screen.
 * Grid columns are 32 pixels apart, so a pad column sits directly above its
 * beat bar. Sixteen cells leave no room for digits, which is no loss: the grid
 * has the same shape as the controller, so position identifies the pad.
 */
#define PAD_COLUMN_PITCH 32u
#define PAD_ROW_PITCH 5u
#define PAD_ORIGIN_X 2u
#define PAD_GRID_TOP 7u
#define PAD_WIDTH 28u
#define PAD_HEIGHT 4u
#define BEAT_BAR_TOP 28u
#define BEAT_BAR_HEIGHT 4u
#define BLINK_PERIOD_MILLISECONDS 300u

static unsigned int pad_column_x(unsigned int column)
{
    return PAD_ORIGIN_X + column * PAD_COLUMN_PITCH;
}

/* Vertical stripes: reads as "sounding" without reading as a solid block. */
static void hatch_rectangle(MusicBoxDisplay *display, unsigned int x,
                            unsigned int y, unsigned int width,
                            unsigned int height)
{
    for (unsigned int column = 0u; column < width; column += 2u)
        fill_rectangle(display, x + column, y, 1u, height);
}

/* Four bars, the current one solid. Shown in every mode as a footer. */
static void draw_beat_footer(MusicBoxDisplay *display)
{
    for (unsigned int beat = 0u; beat < MUSIC_BOX_PAD_COLUMNS; ++beat) {
        const unsigned int x = pad_column_x(beat);
        if (display->beat == beat)
            fill_rectangle(display, x, BEAT_BAR_TOP, PAD_WIDTH, BEAT_BAR_HEIGHT);
        else
            draw_rectangle(display, x, BEAT_BAR_TOP, PAD_WIDTH, BEAT_BAR_HEIGHT);
    }
}

/*
 * One cell per pad, laid out like the controller: slot 0 is bottom-left.
 *
 *   blank     the pad has no note assigned
 *   baseline  mapped but empty
 *   outline   holds audio, silent
 *   solid     sounding
 *   hatched   sounding out this cycle, stopping at the next boundary
 *   blinking  armed or recording
 */
static void draw_pad_grid(MusicBoxDisplay *display)
{
    const unsigned int mode = (unsigned int)display->mode;
    const uint16_t busy =
        (uint16_t)(display->armed_slots[mode] | display->recording_slots[mode]);

    for (unsigned int slot = 0u; slot < MUSIC_BOX_LOOP_COUNT; ++slot) {
        const uint16_t bit = (uint16_t)(1u << slot);
        if ((display->mapped_slots & bit) == 0u) continue;
        const unsigned int x = pad_column_x(slot % MUSIC_BOX_PAD_COLUMNS);
        const unsigned int row =
            MUSIC_BOX_PAD_ROWS - 1u - slot / MUSIC_BOX_PAD_COLUMNS;
        const unsigned int y = PAD_GRID_TOP + row * PAD_ROW_PITCH;

        if ((busy & bit) != 0u) {
            if (display->blink_on) fill_rectangle(display, x, y, PAD_WIDTH, PAD_HEIGHT);
            else draw_rectangle(display, x, y, PAD_WIDTH, PAD_HEIGHT);
        } else if ((display->pending_stop_slots[mode] & bit) != 0u) {
            hatch_rectangle(display, x, y, PAD_WIDTH, PAD_HEIGHT);
        } else if ((display->playing_slots[mode] & bit) != 0u) {
            fill_rectangle(display, x, y, PAD_WIDTH, PAD_HEIGHT);
        } else if ((display->loaded_slots[mode] & bit) != 0u) {
            draw_rectangle(display, x, y, PAD_WIDTH, PAD_HEIGHT);
        } else {
            fill_rectangle(display, x, y + PAD_HEIGHT - 1u, PAD_WIDTH, 1u);
        }
    }
}

/* Lowest set bit, as a 1-based pad number for the header. */
static unsigned int first_slot(uint16_t mask)
{
    unsigned int slot = 0u;
    while (slot < MUSIC_BOX_LOOP_COUNT && (mask & (uint16_t)(1u << slot)) == 0u) ++slot;
    return slot + 1u;
}

static void draw_status_screen(MusicBoxDisplay *display)
{
    const unsigned int mode = (unsigned int)display->mode;
    draw_text(display, PAD_ORIGIN_X, 0u,
              display->mode == MUSIC_BOX_MODE_SAMPLER ? "SAMPLER" : "LOOP");

    /* With sixteen pads the state has to name the pad it belongs to. */
    char right[16];
    if (display->recording_slots[mode] != 0u)
        (void)snprintf(right, sizeof(right), "REC %u",
                       first_slot(display->recording_slots[mode]));
    else if (display->armed_slots[mode] != 0u)
        (void)snprintf(right, sizeof(right), "ARM %u",
                       first_slot(display->armed_slots[mode]));
    else
        (void)snprintf(right, sizeof(right), "%u BPM",
                       (unsigned int)((display->tempo_millibpm + 500u) / 1000u));
    draw_text_right(display, MUSIC_BOX_DISPLAY_WIDTH - 2u, 0u, right);

    draw_pad_grid(display);
}

/* Bar geometry: a 124x13 frame with a 120x9 fill area inside it. */
static void draw_volume_screen(MusicBoxDisplay *display)
{
    const unsigned int percent = display->volume_percent;
    char value[8];
    (void)snprintf(value, sizeof(value), "%u%%", percent);
    draw_text(display, 2u, 0u, "VOLUME");
    draw_text_right(display, MUSIC_BOX_DISPLAY_WIDTH - 2u, 0u, value);
    draw_rectangle(display, 2u, 11u, 124u, 13u);
    fill_rectangle(display, 4u, 13u, (120u * percent + 50u) / 100u, 9u);
}

static bool refresh(MusicBoxDisplay *display)
{
    if (!display->enabled) return true;
    memset(display->pixels, 0, sizeof(display->pixels));
    if (display->volume_visible) draw_volume_screen(display);
    else draw_status_screen(display);
    draw_beat_footer(display);

    const uint8_t position[] = {0x21, 0x00, 0x7f, 0x22, 0x00, 0x03};
    return send_bytes(display->fd, 0x00u, position, sizeof(position)) &&
           send_bytes(display->fd, 0x40u, display->pixels,
                      sizeof(display->pixels));
}

bool music_box_display_init(MusicBoxDisplay *display,
                            const MusicBoxConfig *config)
{
    memset(display, 0, sizeof(*display));
    display->fd = -1;
    display->tempo_millibpm = config->tempo_millibpm;
    display->volume_hold_milliseconds = config->display_volume_seconds * 1000u;
    display->blink_on = true;
    for (unsigned int slot = 0u; slot < MUSIC_BOX_LOOP_COUNT; ++slot)
        if (config->loop_notes[slot] != MUSIC_BOX_NOTE_UNMAPPED)
            display->mapped_slots |= (uint16_t)(1u << slot);
    if (!config->display_enabled) return true;

    display->fd = open(config->display_i2c_device, O_RDWR);
    if (display->fd < 0 ||
        ioctl(display->fd, I2C_SLAVE, config->display_i2c_address) < 0) {
        fprintf(stderr, "Cannot open beat display %s at 0x%02x: %s\n",
                config->display_i2c_device,
                (unsigned int)config->display_i2c_address,
                strerror(errno));
        music_box_display_destroy(display);
        return false;
    }

    const uint8_t initialize[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x1f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x02,
        0x81, 0x8f, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf,
    };
    display->enabled = send_bytes(display->fd, 0x00u, initialize,
                                  sizeof(initialize));
    if (!display->enabled || !refresh(display)) {
        fprintf(stderr, "Cannot initialize beat display: %s\n", strerror(errno));
        music_box_display_destroy(display);
        return false;
    }
    return true;
}

static void commit(MusicBoxDisplay *display)
{
    if (refresh(display)) return;
    fprintf(stderr, "Cannot update the display: %s\n", strerror(errno));
    music_box_display_destroy(display);
}

void music_box_display_handle_event(MusicBoxDisplay *display,
                                    const MusicBoxEvent *event)
{
    if (!display->enabled) return;
    /* The beat index counts a whole bar, which can be longer than the four
       footer bars, so it is not a slot number and skips the slot guard. */
    if (event->type == MUSIC_BOX_EVENT_BEAT) {
        display->beat = event->slot;
        commit(display);
        return;
    }
    if (event->bank >= MUSIC_BOX_MODE_COUNT || event->slot >= MUSIC_BOX_LOOP_COUNT)
        return;
    const unsigned int bank = event->bank;
    const uint16_t bit = (uint16_t)(1u << event->slot);
    const uint16_t clear = (uint16_t)~bit;
    switch (event->type) {
    case MUSIC_BOX_EVENT_RECORD_ARMED:
        display->armed_slots[bank] |= bit;
        /* Light the cell on the way in rather than at the next blink edge. */
        display->blink_on = true;
        break;
    case MUSIC_BOX_EVENT_RECORD_STARTED:
        display->armed_slots[bank] &= clear;
        display->recording_slots[bank] |= bit;
        /* The previous take is gone as soon as the new one starts. */
        display->loaded_slots[bank] &= clear;
        display->playing_slots[bank] &= clear;
        display->pending_stop_slots[bank] &= clear;
        display->blink_on = true;
        break;
    case MUSIC_BOX_EVENT_RECORD_FINISHED:
        display->recording_slots[bank] &= clear;
        display->armed_slots[bank] &= clear;
        display->loaded_slots[bank] |= bit;
        display->pending_stop_slots[bank] &= clear;
        /* A closed loop plays at once; a sample waits to be triggered. */
        if (bank == (unsigned int)MUSIC_BOX_MODE_LOOP) display->playing_slots[bank] |= bit;
        break;
    case MUSIC_BOX_EVENT_LOOP_STOP_ARMED:
        /* Still sounding out this cycle; it goes quiet at the next boundary. */
        display->pending_stop_slots[bank] |= bit;
        break;
    case MUSIC_BOX_EVENT_LOOP_STOP_CANCELLED:
        display->pending_stop_slots[bank] &= clear;
        break;
    case MUSIC_BOX_EVENT_LOOP_STOPPED:
        display->playing_slots[bank] &= clear;
        display->pending_stop_slots[bank] &= clear;
        break;
    case MUSIC_BOX_EVENT_SAMPLE_STARTED:
        display->playing_slots[bank] |= bit;
        break;
    case MUSIC_BOX_EVENT_SAMPLE_FINISHED:
        display->playing_slots[bank] &= clear;
        break;
    case MUSIC_BOX_EVENT_MODE_CHANGED:
        display->mode = (MusicBoxMode)event->value;
        break;
    case MUSIC_BOX_EVENT_TEMPO_CHANGED:
        display->tempo_millibpm = (uint32_t)event->value;
        break;
    default:
        return;
    }
    commit(display);
}

void music_box_display_show_volume(MusicBoxDisplay *display, uint8_t percent)
{
    if (!display->enabled) return;
    display->volume_percent = percent > 100u ? 100u : percent;
    if (display->volume_hold_milliseconds == 0u) return;
    display->volume_visible = true;
    display->volume_hide_at_milliseconds =
        monotonic_milliseconds() + display->volume_hold_milliseconds;
    commit(display);
}

void music_box_display_tick(MusicBoxDisplay *display)
{
    if (!display->enabled) return;
    const uint64_t now = monotonic_milliseconds();
    if (display->volume_visible) {
        if (now < display->volume_hide_at_milliseconds) return;
        display->volume_visible = false;
        commit(display);
        return;
    }
    /* Armed and recording cells blink. This is the screen's only self-driven
       cadence and it runs solely while one of them is pending, so an idle
       Music Box still redraws only when something actually happens. */
    const unsigned int mode = (unsigned int)display->mode;
    if ((display->armed_slots[mode] | display->recording_slots[mode]) == 0u) return;
    if (now < display->blink_at_milliseconds) return;
    display->blink_at_milliseconds = now + BLINK_PERIOD_MILLISECONDS;
    display->blink_on = !display->blink_on;
    commit(display);
}

void music_box_display_destroy(MusicBoxDisplay *display)
{
    if (display == NULL) return;
    if (display->fd >= 0) {
        const uint8_t off = 0xae;
        (void)send_bytes(display->fd, 0x00u, &off, 1u);
        close(display->fd);
    }
    display->fd = -1;
    display->enabled = false;
}
