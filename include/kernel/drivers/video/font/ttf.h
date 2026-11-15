#ifndef KERNEL_DRIVERS_VIDEO_FONT_TTF_H
#define KERNEL_DRIVERS_VIDEO_FONT_TTF_H

#include <stdint.h>

int ttf_load_font(const char* filepath);
void ttf_set_font_size(int font_id, float pixels_height);
void ttf_draw_char(int font_id, int x, int y, uint32_t color, uint32_t codepoint); // <-- char c yerine uint32_t codepoint yapıldı
void ttf_draw_text(int font_id, int x, int y, uint32_t color, const char* text);

#endif