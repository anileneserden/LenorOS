#include <ui/wm.h>
#include <kernel/drivers/video/gfx.h>
#include <ui/desktop.h>
#include <ui/menu.h>

static context_menu_t active_context_menu = {0};

void menu_show(int x, int y, menu_item_t* items, int count, void* context) {
    active_context_menu.x = x;
    active_context_menu.y = y;
    active_context_menu.is_open = true;
    active_context_menu.item_count = count;
    active_context_menu.context_target = context;

    int max_w = 140; // Menü genişliği
    for (int i = 0; i < count && i < 8; i++) {
        active_context_menu.items[i] = items[i];
    }
    
    active_context_menu.width = max_w;
    active_context_menu.height = (count * 24) + 8;
}

void menu_draw(void) {
    if (!active_context_menu.is_open) return;

    int x = active_context_menu.x;
    int y = active_context_menu.y;
    int w = active_context_menu.width;
    int h = active_context_menu.height;

    // Menü Arka Planı (Klasik koyu gri tema)
    gfx_fill_rect(x, y, w, h, 0xFF222222);

    // 3D Çerçeve Efekti
    gfx_fill_rect(x, y, w, 1, 0xFF666666);         // Üst
    gfx_fill_rect(x, y + h - 1, w, 1, 0xFF111111); // Alt
    gfx_fill_rect(x, y, 1, h, 0xFF666666);         // Sol
    gfx_fill_rect(x + w - 1, y, 1, h, 0xFF111111); // Sağ

    // Öğeleri Çiz
    for (int i = 0; i < active_context_menu.item_count; i++) {
        int item_y = y + 4 + (i * 24);
        
        // İsteğe bağlı: Fare üzerindeyse hover efekti eklenebilir
        gfx_draw_text_utf8(x + 12, item_y + 4, 0xFFFFFFFF, active_context_menu.items[i].text);
    }
}

bool menu_handle_click(int mouse_x, int mouse_y) {
    if (!active_context_menu.is_open) return false;

    int x = active_context_menu.x;
    int y = active_context_menu.y;
    int w = active_context_menu.width;
    int h = active_context_menu.height;

    // Menü sınırları içinde mi tıklandı?
    if (mouse_x >= x && mouse_x <= x + w && mouse_y >= y && mouse_y <= y + h) {
        int clicked_index = (mouse_y - (y + 4)) / 24;
        if (clicked_index >= 0 && clicked_index < active_context_menu.item_count) {
            // İlgili eylemi (callback) çalıştır ve hedef nesneyi gönder
            if (active_context_menu.items[clicked_index].action) {
                active_context_menu.items[clicked_index].action(active_context_menu.context_target);
            }
        }
        active_context_menu.is_open = false;
        return true;
    }

    // Menü dışına tıklandıysa menüyü kapat
    active_context_menu.is_open = false;
    return false;
}

bool menu_is_open(void) {
    return active_context_menu.is_open;
}