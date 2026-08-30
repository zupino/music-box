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

/* Flips a block so text already drawn inside it reads as knocked-out pixels. */
static void invert_rectangle(MusicBoxDisplay *display, unsigned int x,
                             unsigned int y, unsigned int width,
                             unsigned int height)
{
    for (unsigned int column = 0u; column < width; ++column) {
        for (unsigned int row = 0u; row < height; ++row) {
            const unsigned int pixel_x = x + column;
            const unsigned int pixel_y = y + row;
            if (pixel_x >= MUSIC_BOX_DISPLAY_WIDTH ||
                pixel_y >= MUSIC_BOX_DISPLAY_HEIGHT)
                continue;
            display->pixels[pixel_x + (pixel_y / 8u) * MUSIC_BOX_DISPLAY_WIDTH] ^=
                (uint8_t)(1u << (pixel_y % 8u));
        }
    }
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
 *   rows 9-23   one box per pad, showing what each pad holds right now;
 *   rows 27-31  the beat footer, present on every screen.
 * Each column is 32 pixels wide, so a pad box sits directly above its beat bar.
 */
#define SLOT_COLUMN_PITCH 32u
#define SLOT_ORIGIN_X 2u
#define SLOT_BOX_WIDTH 28u
#define SLOT_BOX_TOP 9u
#define SLOT_BOX_HEIGHT 15u
#define BEAT_BAR_TOP 27u
#define BEAT_BAR_HEIGHT 5u

static unsigned int slot_column_x(unsigned int index)
{
    return SLOT_ORIGIN_X + index * SLOT_COLUMN_PITCH;
}

/* Four bars, the current one solid. Shown in every mode as a footer. */
static void draw_beat_footer(MusicBoxDisplay *display)
{
    for (unsigned int beat = 0u; beat < 4u; ++beat) {
        const unsigned int x = slot_column_x(beat);
        if (display->beat == beat)
            fill_rectangle(display, x, BEAT_BAR_TOP, SLOT_BOX_WIDTH, BEAT_BAR_HEIGHT);
        else
            draw_rectangle(display, x, BEAT_BAR_TOP, SLOT_BOX_WIDTH, BEAT_BAR_HEIGHT);
    }
}

/*
 * Pad boxes. Sounding pads are inverted, so in loop mode the filled boxes are
 * the loops currently playing back, and in sampler mode an outlined box is a
 * pad with a sample ready to trigger.
 */
static void draw_slot_boxes(MusicBoxDisplay *display)
{
    const unsigned int mode = (unsigned int)display->mode;
    for (unsigned int slot = 0u; slot < MUSIC_BOX_LOOP_COUNT; ++slot) {
        const unsigned int x = slot_column_x(slot);
        const uint8_t bit = (uint8_t)(1u << slot);
        const char number[2] = {(char)('1' + slot), '\0'};
        draw_text(display, x + (SLOT_BOX_WIDTH - 5u) / 2u, SLOT_BOX_TOP + 4u, number);
        if ((display->playing_slots[mode] & bit) != 0u) {
            invert_rectangle(display, x, SLOT_BOX_TOP, SLOT_BOX_WIDTH, SLOT_BOX_HEIGHT);
            continue;
        }
        if ((display->recording_slots[mode] & bit) != 0u)
            draw_rectangle(display, x + 1u, SLOT_BOX_TOP + 1u, SLOT_BOX_WIDTH - 2u,
                           SLOT_BOX_HEIGHT - 2u);
        if (((display->loaded_slots[mode] | display->recording_slots[mode] |
              display->armed_slots[mode]) & bit) != 0u)
            draw_rectangle(display, x, SLOT_BOX_TOP, SLOT_BOX_WIDTH, SLOT_BOX_HEIGHT);
    }
}

static void draw_status_screen(MusicBoxDisplay *display)
{
    const unsigned int mode = (unsigned int)display->mode;
    draw_text(display, SLOT_ORIGIN_X, 0u,
              display->mode == MUSIC_BOX_MODE_SAMPLER ? "SAMPLER" : "LOOP");

    char tempo[16];
    (void)snprintf(tempo, sizeof(tempo), "%u BPM",
                   (unsigned int)((display->tempo_millibpm + 500u) / 1000u));
    const char *right = tempo;
    if (display->recording_slots[mode] != 0u) right = "REC";
    else if (display->armed_slots[mode] != 0u) right = "ARMED";
    draw_text_right(display, MUSIC_BOX_DISPLAY_WIDTH - 2u, 0u, right);

    draw_slot_boxes(display);
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
    const uint8_t bit = (uint8_t)(1u << event->slot);
    switch (event->type) {
    case MUSIC_BOX_EVENT_RECORD_ARMED:
        display->armed_slots[bank] |= bit;
        break;
    case MUSIC_BOX_EVENT_RECORD_STARTED:
        display->armed_slots[bank] &= (uint8_t)~bit;
        display->recording_slots[bank] |= bit;
        /* The previous take is gone as soon as the new one starts. */
        display->loaded_slots[bank] &= (uint8_t)~bit;
        display->playing_slots[bank] &= (uint8_t)~bit;
        break;
    case MUSIC_BOX_EVENT_RECORD_FINISHED:
        display->recording_slots[bank] &= (uint8_t)~bit;
        display->armed_slots[bank] &= (uint8_t)~bit;
        display->loaded_slots[bank] |= bit;
        /* A closed loop plays at once; a sample waits to be triggered. */
        if (bank == (unsigned int)MUSIC_BOX_MODE_LOOP) display->playing_slots[bank] |= bit;
        break;
    case MUSIC_BOX_EVENT_LOOP_STOPPED:
        display->playing_slots[bank] &= (uint8_t)~bit;
        break;
    case MUSIC_BOX_EVENT_SAMPLE_STARTED:
        display->playing_slots[bank] |= bit;
        break;
    case MUSIC_BOX_EVENT_SAMPLE_FINISHED:
        display->playing_slots[bank] &= (uint8_t)~bit;
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
    if (!display->enabled || !display->volume_visible) return;
    if (monotonic_milliseconds() < display->volume_hide_at_milliseconds) return;
    display->volume_visible = false;
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
