#include <ui/window.h>
#include <kernel/drivers/video/gfx.h>

void window_draw(window_t* win) {
    if (!win) return;

    int radius = 8; // Window corner rounding radius

    // 1. Pencere gövdesi (yuvarlatılmış arka plan)
    gfx_fill_rounded_rect(win->x, win->y, win->width, win->height, radius, 0xFFE0E0E0);

    // 2. Başlık çubuğu (Title bar)
    uint32_t title_bar_color = win->is_active ? 0xFF005A9E : 0xFF606060;
    gfx_fill_rect(win->x, win->y, win->width, WINDOW_TITLE_HEIGHT, title_bar_color);

    // 3. Yan yana 3 adet dikdörtgen buton (Minimize, Maximize, Close)
    int btn_width = 36;  // Her bir butonun genişliği
    int btn_height = WINDOW_TITLE_HEIGHT; // Başlık çubuğu yüksekliğiyle tam uyumlu
    int total_buttons_width = btn_width * 3;
    int start_x = win->x + win->width - total_buttons_width;
    int btn_y = win->y;

    // --- Buton 1: Küçült / Minimize (-) ---
    gfx_fill_rect(start_x, btn_y, btn_width, btn_height, title_bar_color);
    // İsteğe bağlı: İçine çizgi ikon çizilebilir veya boş bırakılabilir

    // --- Buton 2: Büyüt / Maximize (▢) ---
    gfx_fill_rect(start_x + btn_width, btn_y, btn_width, btn_height, title_bar_color);

    // --- Buton 3: Kapat / Close (X) ---
    // Klasik Windows tarzı kırmızı kapatma butonu
    gfx_fill_rect(start_x + (btn_width * 2), btn_y, btn_width, btn_height, 0xFFE81123);
    
    // Eğer PNG ikon yüklemek istiyorsan, bu kırmızı dikdörtgenin tam ortasına 
    // mevcut PNG loader fonksiyonun ile X ikonunu buraya blit edebilirsin:
    // png_draw_at("red-x-icon.png", start_x + (btn_width * 2) + (btn_width - icon_w) / 2, btn_y + (btn_height - icon_h) / 2);
}