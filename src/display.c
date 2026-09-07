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
    {'I', {0x00, 0x41, 0x7f, 0x41, 0x00}},
    {'L', {0x7f, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7f, 0x02, 0x0c, 0x02, 0x7f}},
    {'N', {0x7f, 0x02, 0x04, 0x08, 0x7f}},
    {'O', {0x3e, 0x41, 0x41, 0x41, 0x3e}},
    {'P', {0x7f, 0x09, 0x09, 0x09, 0x06}},
    {'R', {0x7f, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'U', {0x3f, 0x40, 0x40, 0x40, 0x3f}},
    {'V', {0x1f, 0x20, 0x40, 0x20, 0x1f}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
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
 * Screen layout, 128x32. The panel is wide and short, so nothing is stacked:
 * the pad grid, the status text and the beat run as three columns, which lets
 * the grid use the full height instead of sharing it with a header and footer.
 *
 *   x 3-36    rows 0-30   a 4x4 square grid mirroring the MPD218 pads
 *   x 40      rows 0-31   a rule separating grid from status
 *   x 44-116  rows 0-30  mode, tempo and record/source state
 *   x 120-124 rows 0-30   the beat, four dots, the current one filled
 *
 * The major column gutters and outer margins are 3px; square pad cells use a
 * tighter 2px horizontal gap. The dots share the grid's row pitch and the text
 * lines are flush with its top and bottom, so all three columns run on one
 * rhythm. Sixteen cells leave no room for digits, which is no loss: the grid
 * has the same shape as the controller, so position identifies the pad.
 */
#define PAD_CELL_WIDTH 7u
#define PAD_CELL_HEIGHT 7u
#define PAD_CELL_GAP_X 2u
#define PAD_CELL_GAP_Y 1u
#define PAD_ORIGIN_X 3u
#define PAD_GUTTER 3u
#define PAD_COLUMN_PITCH (PAD_CELL_WIDTH + PAD_CELL_GAP_X)
#define PAD_ROW_PITCH (PAD_CELL_HEIGHT + PAD_CELL_GAP_Y)
#define PAD_GRID_WIDTH \
    (MUSIC_BOX_PAD_COLUMNS * PAD_COLUMN_PITCH - PAD_CELL_GAP_X)
#define PAD_GRID_HEIGHT \
    (MUSIC_BOX_PAD_ROWS * PAD_ROW_PITCH - PAD_CELL_GAP_Y)

#define STATUS_RULE_X (PAD_ORIGIN_X + PAD_GRID_WIDTH + PAD_GUTTER)
#define STATUS_TEXT_X (STATUS_RULE_X + 1u + PAD_GUTTER)
/* Three lines spread over exactly the grid's height: flush top and bottom. */
#define STATUS_LINE_PITCH ((PAD_GRID_HEIGHT - 7u) / 2u)

/* A rail of four dots, one per beat, on the grid's own row pitch. */
#define BEAT_DOT_SIZE 5u
#define BEAT_DOT_X (MUSIC_BOX_DISPLAY_WIDTH - PAD_GUTTER - BEAT_DOT_SIZE)
#define BEAT_DOT_IDLE_SIZE 3u
/* Everything except the beat rail has to stay left of this. */
#define CONTENT_RIGHT_X (BEAT_DOT_X - PAD_GUTTER)

#define BLINK_PERIOD_MILLISECONDS 300u

/* Vertical stripes: reads as "sounding" without reading as a solid block. */
static void hatch_rectangle(MusicBoxDisplay *display, unsigned int x,
                            unsigned int y, unsigned int width,
                            unsigned int height)
{
    for (unsigned int column = 0u; column < width; column += 2u)
        fill_rectangle(display, x + column, y, 1u, height);
}

static unsigned int pad_row_y(unsigned int row)
{
    return row * PAD_ROW_PITCH;
}

/*
 * The beat, as a column down the right edge rather than a strip along the
 * bottom: the panel has width to spare and no vertical room to waste. The
 * current beat is a filled square, the rest small dots. Present on every
 * screen, and beat one is the top dot.
 */
static void draw_beat_rail(MusicBoxDisplay *display)
{
    for (unsigned int beat = 0u; beat < MUSIC_BOX_PAD_ROWS; ++beat) {
        const unsigned int y = pad_row_y(beat) + (PAD_CELL_HEIGHT - BEAT_DOT_SIZE) / 2u;
        if (display->beat == beat) {
            fill_rectangle(display, BEAT_DOT_X, y, BEAT_DOT_SIZE, BEAT_DOT_SIZE);
        } else {
            const unsigned int inset = (BEAT_DOT_SIZE - BEAT_DOT_IDLE_SIZE) / 2u;
            fill_rectangle(display, BEAT_DOT_X + inset, y + inset,
                           BEAT_DOT_IDLE_SIZE, BEAT_DOT_IDLE_SIZE);
        }
    }
}

/*
 * One cell per pad, laid out in printed-number order: slot 0 / PAD 1 is
 * top-left, and slots advance left-to-right then top-to-bottom.
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
    const unsigned int mode = display->mode == MUSIC_BOX_MODE_FILES
        ? (unsigned int)MUSIC_BOX_MODE_SAMPLER
        : (unsigned int)display->mode;
    const uint16_t busy =
        (uint16_t)(display->armed_slots[mode] | display->recording_slots[mode]);

    for (unsigned int slot = 0u; slot < MUSIC_BOX_LOOP_COUNT; ++slot) {
        const uint16_t bit = (uint16_t)(1u << slot);
        if ((display->mapped_slots & bit) == 0u) continue;
        const unsigned int x =
            PAD_ORIGIN_X + (slot % MUSIC_BOX_PAD_COLUMNS) * PAD_COLUMN_PITCH;
        const unsigned int y = pad_row_y(slot / MUSIC_BOX_PAD_COLUMNS);

        if ((busy & bit) != 0u) {
            if (display->blink_on) fill_rectangle(display, x, y, PAD_CELL_WIDTH, PAD_CELL_HEIGHT);
            else draw_rectangle(display, x, y, PAD_CELL_WIDTH, PAD_CELL_HEIGHT);
        } else if ((display->pending_stop_slots[mode] & bit) != 0u) {
            hatch_rectangle(display, x, y, PAD_CELL_WIDTH, PAD_CELL_HEIGHT);
        } else if ((display->playing_slots[mode] & bit) != 0u) {
            fill_rectangle(display, x, y, PAD_CELL_WIDTH, PAD_CELL_HEIGHT);
        } else if ((display->loaded_slots[mode] & bit) != 0u) {
            draw_rectangle(display, x, y, PAD_CELL_WIDTH, PAD_CELL_HEIGHT);
        } else {
            fill_rectangle(display, x, y + PAD_CELL_HEIGHT - 1u, PAD_CELL_WIDTH, 1u);
        }
    }
}

/* Lowest set bit, as a 1-based pad number for the status column. */
static unsigned int first_slot(uint16_t mask)
{
    unsigned int slot = 0u;
    while (slot < MUSIC_BOX_LOOP_COUNT && (mask & (uint16_t)(1u << slot)) == 0u) ++slot;
    return slot + 1u;
}

static uint16_t all_recording_slots(const MusicBoxDisplay *display)
{
    uint16_t slots = 0u;
    for (unsigned int mode = 0u; mode < MUSIC_BOX_MODE_COUNT; ++mode)
        slots |= display->recording_slots[mode];
    return slots;
}

/* Seven characters fit beside the grid, which is exactly "SAMPLER". */
static void draw_status_column(MusicBoxDisplay *display)
{
    static const char *const labels[MUSIC_BOX_MODE_COUNT] = {
        "LOOP", "SAMPLER", "PAD", "MELODY", "FILES"
    };
    const unsigned int mode = (unsigned int)display->mode;
    const unsigned int state_mode = display->mode == MUSIC_BOX_MODE_FILES
        ? (unsigned int)MUSIC_BOX_MODE_SAMPLER : mode;

    /* The rule runs the full height so the right half reads as one column. */
    fill_rectangle(display, STATUS_RULE_X, 0u, 1u, MUSIC_BOX_DISPLAY_HEIGHT);
    draw_text(display, STATUS_TEXT_X, 0u, labels[mode]);

    char tempo[16];
    (void)snprintf(tempo, sizeof(tempo), "%u BPM",
                   (unsigned int)((display->tempo_millibpm + 500u) / 1000u));
    draw_text(display, STATUS_TEXT_X, STATUS_LINE_PITCH, tempo);

    /* With sixteen pads the state has to name the pad it belongs to. */
    char state[16] = "";
    const uint16_t recording = all_recording_slots(display);
    if (recording != 0u)
        (void)snprintf(state, sizeof(state), "REC %s S%u",
                       display->recording_source == MUSIC_BOX_RECORD_MONITOR
                           ? "MON" : "MIC",
                       first_slot(recording));
    else if (display->armed_slots[state_mode] != 0u)
        (void)snprintf(state, sizeof(state), "ARM %u",
                       first_slot(display->armed_slots[state_mode]));
    else if (display->mode == MUSIC_BOX_MODE_LOOP ||
             display->mode == MUSIC_BOX_MODE_MELODY)
        (void)snprintf(state, sizeof(state), "S%u",
                       (unsigned int)display->selected_sample + 1u);
    else if (display->mode == MUSIC_BOX_MODE_SAMPLER)
        /* Pads still choose recording/playback slots. This label is explicitly
           the K4 trim target, which needs to remain visible before K5/K6 move. */
        (void)snprintf(state, sizeof(state), "TRIM S%u",
                       (unsigned int)display->selected_sample + 1u);
    else if (display->mode == MUSIC_BOX_MODE_FILES)
        (void)snprintf(state, sizeof(state), "TAP/HOLD");
    if (state[0] != '\0')
        draw_text(display, STATUS_TEXT_X, 2u * STATUS_LINE_PITCH, state);
}

static void draw_status_screen(MusicBoxDisplay *display)
{
    draw_pad_grid(display);
    draw_status_column(display);
}

static void draw_trim_screen(MusicBoxDisplay *display)
{
    char title[16];
    (void)snprintf(title, sizeof(title), "TRIM S%u",
                   (unsigned int)display->trim_slot + 1u);
    draw_text(display, 2u, 0u, title);

    const unsigned int x = 2u;
    const unsigned int y = 17u;
    const unsigned int width = MUSIC_BOX_DISPLAY_WIDTH - 4u;
    draw_rectangle(display, x, y, width, 11u);
    const unsigned int inner = width - 4u;
    const unsigned int start = (inner * display->trim_start) / 127u;
    const unsigned int end = (inner * display->trim_end) / 127u;
    if (end > start)
        fill_rectangle(display, x + 2u + start, y + 2u, end - start, 7u);
}

/* Shares the pad grid's margins and stops short of the beat rail. */
#define VOLUME_BAR_WIDTH (CONTENT_RIGHT_X - PAD_ORIGIN_X)
#define VOLUME_FILL_WIDTH (VOLUME_BAR_WIDTH - 4u)

static void draw_volume_screen(MusicBoxDisplay *display)
{
    const unsigned int percent = display->volume_percent;
    char value[8];
    (void)snprintf(value, sizeof(value), "%u%%", percent);
    draw_text(display, PAD_ORIGIN_X, 0u, "VOLUME");
    draw_text_right(display, CONTENT_RIGHT_X, 0u, value);
    draw_rectangle(display, PAD_ORIGIN_X, 11u, VOLUME_BAR_WIDTH, 13u);
    fill_rectangle(display, PAD_ORIGIN_X + 2u, 13u,
                   (VOLUME_FILL_WIDTH * percent + 50u) / 100u, 9u);
}

static bool refresh(MusicBoxDisplay *display)
{
    if (!display->enabled) return true;
    memset(display->pixels, 0, sizeof(display->pixels));
    /* Source confirmation is most important at the imprecise K3 toggle, so a
       temporary trim or volume overlay must never hide an active recording. */
    if (all_recording_slots(display) != 0u) draw_status_screen(display);
    else if (display->volume_visible) draw_volume_screen(display);
    else if (display->trim_visible) draw_trim_screen(display);
    else draw_status_screen(display);
    draw_beat_rail(display);

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
    display->mode = MUSIC_BOX_MODE_PAD;
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

    const uint8_t segment_direction = config->display_rotate_180 ? 0xa0u : 0xa1u;
    const uint8_t scan_direction = config->display_rotate_180 ? 0xc0u : 0xc8u;
    const uint8_t initialize[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x1f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x00, segment_direction, scan_direction, 0xda, 0x02,
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

/*
 * One failed I2C transfer is not worth losing the screen for the rest of the
 * run; the bus recovers. This matters more than it used to, because the screen
 * now redraws on pad events and on the blink, so it writes far more often than
 * when it only showed a beat.
 */
#define DISPLAY_WRITE_FAILURE_LIMIT 5u
#define TRIM_HOLD_MILLISECONDS 1500u

static void commit(MusicBoxDisplay *display)
{
    if (refresh(display)) {
        display->write_failures = 0u;
        return;
    }
    if (++display->write_failures < DISPLAY_WRITE_FAILURE_LIMIT) return;
    fprintf(stderr, "Giving up on the display after %u failed frames: %s\n",
            display->write_failures, strerror(errno));
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
        display->dirty = true;
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
        display->recording_source = (MusicBoxRecordingSource)event->value;
        display->armed_slots[bank] &= clear;
        display->recording_slots[bank] |= bit;
        /* The previous take is gone as soon as the new one starts. */
        display->loaded_slots[bank] &= clear;
        display->playing_slots[bank] &= clear;
        display->pending_stop_slots[bank] &= clear;
        display->blink_on = true;
        break;
    case MUSIC_BOX_EVENT_RECORD_FINISHED:
    case MUSIC_BOX_EVENT_RESTORED_SAMPLE:
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
    case MUSIC_BOX_EVENT_SLOT_STARTED:
        display->playing_slots[bank] |= bit;
        break;
    case MUSIC_BOX_EVENT_SLOT_FINISHED:
        display->playing_slots[bank] &= clear;
        break;
    case MUSIC_BOX_EVENT_MODE_CHANGED:
        display->mode = (MusicBoxMode)event->value;
        break;
    case MUSIC_BOX_EVENT_TEMPO_CHANGED:
        display->tempo_millibpm = (uint32_t)event->value;
        break;
    case MUSIC_BOX_EVENT_LOOP_SELECTED:
        display->selected_loop = event->slot;
        break;
    case MUSIC_BOX_EVENT_SAMPLE_SELECTED:
        display->selected_sample = event->slot;
        break;
    case MUSIC_BOX_EVENT_SAMPLE_ASSIGNED:
    case MUSIC_BOX_EVENT_RESTORED_LOOP:
        display->loaded_slots[MUSIC_BOX_MODE_LOOP] |= bit;
        display->playing_slots[MUSIC_BOX_MODE_LOOP] &= clear;
        display->pending_stop_slots[MUSIC_BOX_MODE_LOOP] &= clear;
        break;
    case MUSIC_BOX_EVENT_SAMPLE_TRIMMED:
    case MUSIC_BOX_EVENT_SAMPLE_ASSIGN_FAILED:
    case MUSIC_BOX_EVENT_SAMPLE_BANK_FULL:
    case MUSIC_BOX_EVENT_RECORDING_BUSY:
        return;
    case MUSIC_BOX_EVENT_SAMPLE_DELETED:
        display->loaded_slots[MUSIC_BOX_MODE_SAMPLER] &= clear;
        display->playing_slots[MUSIC_BOX_MODE_SAMPLER] &= clear;
        display->loaded_slots[MUSIC_BOX_MODE_LOOP] &= (uint16_t)~event->value;
        display->playing_slots[MUSIC_BOX_MODE_LOOP] &= (uint16_t)~event->value;
        display->pending_stop_slots[MUSIC_BOX_MODE_LOOP] &= (uint16_t)~event->value;
        break;
    case MUSIC_BOX_EVENT_TRIM_CHANGED:
        display->trim_slot = event->slot;
        display->trim_start = (uint8_t)(event->value & 0xffu);
        display->trim_end = (uint8_t)((event->value >> 8u) & 0xffu);
        display->trim_visible = true;
        display->trim_hide_at_milliseconds =
            monotonic_milliseconds() + TRIM_HOLD_MILLISECONDS;
        break;
    default:
        return;
    }
    display->dirty = true;
}

void music_box_display_show_volume(MusicBoxDisplay *display, uint8_t percent)
{
    if (!display->enabled) return;
    display->volume_percent = percent > 100u ? 100u : percent;
    if (display->volume_hold_milliseconds == 0u) return;
    display->volume_visible = true;
    display->volume_hide_at_milliseconds =
        monotonic_milliseconds() + display->volume_hold_milliseconds;
    display->dirty = true;
}

void music_box_display_tick(MusicBoxDisplay *display)
{
    if (!display->enabled) return;
    const uint64_t now = monotonic_milliseconds();
    if (display->volume_visible) {
        if (now >= display->volume_hide_at_milliseconds) {
            display->volume_visible = false;
            display->dirty = true;
        }
    }
    if (display->trim_visible) {
        if (now >= display->trim_hide_at_milliseconds) {
            display->trim_visible = false;
            display->dirty = true;
        }
    }
    /* Armed and recording cells blink. This is the screen's only self-driven
       cadence and it runs solely while one of them is pending, so an idle
       Music Box still redraws only when something actually happens. */
    const unsigned int mode = (unsigned int)display->mode;
    if ((display->armed_slots[mode] | all_recording_slots(display)) != 0u &&
        now >= display->blink_at_milliseconds) {
        display->blink_at_milliseconds = now + BLINK_PERIOD_MILLISECONDS;
        display->blink_on = !display->blink_on;
        display->dirty = true;
    }
    if (display->dirty) {
        display->dirty = false;
        commit(display);
    }
}

bool music_box_display_active(const MusicBoxDisplay *display)
{
    return display != NULL && display->enabled;
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
