// SPDX-License-Identifier: MIT
#include "display.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
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
    {'M', {0x7f, 0x02, 0x0c, 0x02, 0x7f}},
    {'P', {0x7f, 0x09, 0x09, 0x09, 0x06}},
    {'R', {0x7f, 0x09, 0x19, 0x29, 0x46}},
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

static void draw_note(MusicBoxDisplay *display)
{
    for (unsigned int y = 1u; y < 8u; ++y) set_pixel(display, 7u, y);
    for (unsigned int x = 7u; x < 11u; ++x) set_pixel(display, x, 1u);
    set_pixel(display, 5u, 7u);
    set_pixel(display, 6u, 6u);
    set_pixel(display, 6u, 7u);
}

static void draw_circle(MusicBoxDisplay *display, unsigned int center_x,
                        bool filled)
{
    for (int y = -4; y <= 4; ++y) {
        for (int x = -4; x <= 4; ++x) {
            const int distance = x * x + y * y;
            if ((filled && distance <= 16) ||
                (!filled && distance >= 11 && distance <= 19))
                set_pixel(display, center_x + (unsigned int)x, 17u + (unsigned int)y);
        }
    }
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

static bool refresh(MusicBoxDisplay *display)
{
    if (!display->enabled) return true;
    memset(display->pixels, 0, sizeof(display->pixels));
    draw_note(display);

    char tempo[16];
    (void)snprintf(tempo, sizeof(tempo), "%u BPM",
                   (unsigned int)((display->tempo_millibpm + 500u) / 1000u));
    draw_text(display, 15u, 0u, tempo);
    if (display->recording_slots != 0u) draw_text(display, 104u, 0u, "REC");
    else if (display->armed_slots != 0u) draw_text(display, 98u, 0u, "ARMED");

    static const unsigned int centers[4] = {14u, 42u, 70u, 98u};
    for (unsigned int beat = 0u; beat < 4u; ++beat) {
        draw_circle(display, centers[beat], display->beat == beat);
        char number[2] = {(char)('1' + beat), '\0'};
        draw_text(display, centers[beat] - 2u, 25u, number);
    }

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

void music_box_display_handle_event(MusicBoxDisplay *display,
                                    const MusicBoxEvent *event)
{
    if (!display->enabled) return;
    switch (event->type) {
    case MUSIC_BOX_EVENT_BEAT:
        display->beat = event->slot;
        break;
    case MUSIC_BOX_EVENT_RECORD_ARMED:
        display->armed_slots |= (uint8_t)(1u << event->slot);
        break;
    case MUSIC_BOX_EVENT_RECORD_STARTED:
        display->armed_slots &= (uint8_t)~(1u << event->slot);
        display->recording_slots |= (uint8_t)(1u << event->slot);
        break;
    case MUSIC_BOX_EVENT_RECORD_FINISHED:
        display->recording_slots &= (uint8_t)~(1u << event->slot);
        display->armed_slots &= (uint8_t)~(1u << event->slot);
        break;
    case MUSIC_BOX_EVENT_TEMPO_CHANGED:
        display->tempo_millibpm = (uint32_t)event->value;
        break;
    default:
        return;
    }
    if (!refresh(display)) {
        fprintf(stderr, "Cannot update beat display: %s\n", strerror(errno));
        music_box_display_destroy(display);
    }
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
