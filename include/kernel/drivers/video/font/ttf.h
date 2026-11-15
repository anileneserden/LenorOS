#ifndef TTF_H
#define TTF_H

#include <stdint.h>
#include <stdbool.h>

// Font yükleme ve yönetim fonksiyonları
int ttf_load_font(const char* filepath);
void ttf_set_font_size(int font_id, float pixels_height);
void ttf_draw_char(int font_id, int x, int y, uint32_t color, char c);
void ttf_draw_text(int font_id, int x, int y, uint32_t color, const char* text);

#endif