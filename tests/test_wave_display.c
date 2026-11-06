// SPDX-License-Identifier: MIT
#include "../src/display.c"
#include <assert.h>
int main(void)
{
    assert(glyph_for('F')[0] == 0x7f);
    const char *labels = "FILES WAVE SIN TRI SQR SAW C Q R TAP/HOLD";
    for (const char *c = labels; *c; ++c)
        if (*c != ' ') assert(glyph_for(*c) != font[0].columns);
    MusicBoxDisplay d = {0};
    d.enabled = true;
    d.mode = MUSIC_BOX_MODE_WAVE;
    d.wave_parameters[1] = 100;
    d.wave_parameters[3] = 45;
    draw_wave_screen(&d);
    uint8_t before[sizeof(d.pixels)];
    memcpy(before, d.pixels, sizeof(before));
    const uint8_t values[] = {100, 20, 127, 127};
    for (uint8_t parameter = 0; parameter < 4; ++parameter) {
        MusicBoxEvent event = {.type = MUSIC_BOX_EVENT_WAVE_PARAMETER_CHANGED,
            .bank = MUSIC_BOX_MODE_WAVE, .slot = parameter,
            .value = values[parameter]};
        music_box_display_handle_event(&d, &event);
        assert(d.dirty && d.wave_parameters[parameter] == values[parameter]);
        memset(d.pixels, 0, sizeof(d.pixels));
        draw_wave_screen(&d);
        assert(memcmp(before, d.pixels, sizeof(before)) != 0);
        memcpy(before, d.pixels, sizeof(before));
    }
    puts("wave display and Files glyph tests passed");
}
