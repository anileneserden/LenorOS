#ifndef UI_CURSOR_H
#define UI_CURSOR_H

#include <stdint.h>
#include <stdbool.h>

#define CURSOR_WIDTH  16
#define CURSOR_HEIGHT 16

// İmleç Türleri Tanımı
typedef enum {
    CURSOR_ARROW = 0, // Normal ok
    CURSOR_HAND,      // El imleci (Linkler ve Butonlar için)
    CURSOR_IBEAM      // Metin seçme / I-Beam (Input alanları için)
} cursor_type_t;

void cursor_init(void);
void cursor_update_and_redraw(void);
void cursor_hide(void); // Remove the cursor before drawing windows or the screen
void cursor_prepare_redraw(void); // Restore the cursor area to the back buffer without copying to VRAM
void cursor_get_position(int32_t* x, int32_t* y);
void cursor_show(void); // Draw the cursor again after drawing is complete
void cursor_refresh_background(void);
void cursor_sync_position(void);
void cursor_show_internal(bool blit);

// Aktif imleç türünü değiştiren fonksiyon bildirimi
void cursor_set_type(cursor_type_t type);
cursor_type_t cursor_get_type(void);

int32_t cursor_get_x(void);
int32_t cursor_get_y(void);
int32_t cursor_get_old_x(void);
int32_t cursor_get_old_y(void);
int32_t cursor_get_width(void);
int32_t cursor_get_height(void);

#endif