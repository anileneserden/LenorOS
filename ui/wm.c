#include <ui/wm.h>
#include <ui/desktop.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <ui/cursor.h>
#include <kernel/loader/lex.h>
#include <stddef.h>

extern int32_t mouse_x;
extern int32_t mouse_y;
extern uint8_t mouse_buttons;
extern int system_font_id;

#define WM_TITLEBAR_HEIGHT 24
#define RESIZE_BORDER 6
#define MIN_WINDOW_WIDTH 120
#define MIN_WINDOW_HEIGHT 80

typedef enum {
    RESIZE_NONE = 0,
    RESIZE_TOP = 1,
    RESIZE_BOTTOM = 2,
    RESIZE_LEFT = 4,
    RESIZE_RIGHT = 8,
    RESIZE_TOP_LEFT = RESIZE_TOP | RESIZE_LEFT,
    RESIZE_TOP_RIGHT = RESIZE_TOP | RESIZE_RIGHT,
    RESIZE_BOTTOM_LEFT = RESIZE_BOTTOM | RESIZE_LEFT,
    RESIZE_BOTTOM_RIGHT = RESIZE_BOTTOM | RESIZE_RIGHT
} resize_dir_t;

static window_t window_list[MAX_WINDOWS];
static int window_count = 0;
static window_t* active_window = 0;
static text_input_t* active_input = 0;
static window_t* dragged_window = 0;
static window_t* resized_window = 0;
static window_t* hovered_window = 0;
static uint8_t hovered_close_button = 0;
static uint8_t hovered_min_button = 0;
static uint8_t hovered_max_button = 0;
static int resize_direction = RESIZE_NONE;
static uint8_t prev_buttons = 0;
static int resize_old_x = 0;
static int resize_old_y = 0;
static int resize_old_w = 0;
static int resize_old_h = 0;
static uint32_t last_click_time = 0;
static window_t* last_clicked_win = 0;
static uint32_t global_wm_ticks = 0;
static label_item_t* selecting_label = 0;
static char clipboard_buffer[256] = {0};

void wm_init(void) {
    window_count = 0;
    active_window = 0;
    dragged_window = 0;
    resized_window = 0;
    hovered_window = 0;
    hovered_close_button = 0;
    active_input = 0;
    resize_direction = RESIZE_NONE;
    prev_buttons = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_list[i].is_active = false;
        window_list[i].is_dragging = false;
        window_list[i].width = 0;
    }
}

window_t* wm_create_window_without_tabs(int width, int height, const char* title) {
    if (window_count >= MAX_WINDOWS) return 0;

    window_t* win = &window_list[window_count];
    win->x = 60 + (window_count * 30);
    win->y = 60 + (window_count * 30);
    win->is_active = true;
    win->is_dragging = false;
    win->drag_offset_x = 0;
    win->drag_offset_y = 0;
    win->has_tabs = false;
    win->tab_count = 0;
    win->active_tab_index = 0;

    // Calculate minimum width based on title length (including [X] button and padding)
    int title_len = 0;
    while (title[title_len] != '\0' && title_len < 31) {
        win->title[title_len] = title[title_len];
        title_len++;
    }
    win->title[title_len] = '\0';

    int calculated_min_w = (title_len * 8) + 50;
    int global_min_w = 340; 
    
    if (calculated_min_w < global_min_w) {
        calculated_min_w = global_min_w;
    }

    win->min_width = calculated_min_w;
    win->min_height = 240;

    win->width = width < win->min_width ? win->min_width : width;
    win->height = height < win->min_height ? win->min_height : height;

    // Assign reference dimensions when the window is first created
    win->init_win_w = win->width;
    win->init_win_h = win->height;

    for (int j = 0; j < window_count; j++) {
        window_list[j].is_active = false;
    }

    win->is_active = true;
    active_window = win;
    window_count++;

    for (int l = 0; l < win->label_count; l++) {
        win->labels[l].init_x = win->labels[l].x;
        win->labels[l].init_y = win->labels[l].y;
        win->labels[l].init_win_w = win->width;
        win->labels[l].init_win_h = win->height;
    }

    // Combobox başlangıç (init) referans değerlerini kaydet
    for (int c = 0; c < win->combobox_count; c++) {
        win->comboboxes[c].init_x = win->comboboxes[c].x;
        win->comboboxes[c].init_y = win->comboboxes[c].y;
        win->comboboxes[c].init_width = win->comboboxes[c].width;
        win->comboboxes[c].init_height = win->comboboxes[c].height;
        win->comboboxes[c].init_win_w = win->width;
        win->comboboxes[c].init_win_h = win->height;
    }

    damage_union_rect(0, 0, fb_get_width(), fb_get_height());
    desktop_redraw();
    return win;
}

window_t* wm_create_window_with_tabs(int width, int height, const char* title, int initial_tab_count) {
    window_t* win = wm_create_window_without_tabs(width, height, title);
    if (!win) return 0;

    win->has_tabs = true;
    win->tab_count = initial_tab_count > MAX_TABS ? MAX_TABS : initial_tab_count;
    win->active_tab_index = 0;

    for (int i = 0; i < win->tab_count; i++) {
        snprintf(win->tabs[i].title, sizeof(win->tabs[i].title), "Sekme %d", i + 1);
    }

    return win;
}

static void wm_close_tab(window_t* win, int tab_idx) {
    if (!win || win->tab_count <= 0) return;

    // Eğer tek sekme kaldıysa pencereyi tamamen kapat
    if (win->tab_count <= 1) {
        wm_close_window(win);
        return;
    }

    // Kapatılan sekmenin ikon belleği varsa serbest bırak
    if (win->tabs[tab_idx].icon_pixels) {
        kfree(win->tabs[tab_idx].icon_pixels);
        win->tabs[tab_idx].icon_pixels = 0;
    }

    // Sağdaki sekmeleri sola kaydır
    for (int i = tab_idx; i < win->tab_count - 1; i++) {
        win->tabs[i] = win->tabs[i + 1];
    }
    win->tab_count--;

    // Aktif sekme indeksini güncelle
    if (win->active_tab_index >= win->tab_count) {
        win->active_tab_index = win->tab_count - 1;
    } else if (win->active_tab_index > tab_idx) {
        win->active_tab_index--;
    }
}

void wm_close_window(window_t* win) {
    if (!win) return;

    win->width = 0;

    if (active_window == win) {
        active_window = 0;
    }

    for (int i = 0; i < window_count; i++) {
        if (window_list[i].width > 0) {
            window_list[i].is_active = (&window_list[i] == active_window);
        } else {
            window_list[i].is_active = false;
        }
    }

    damage_union_rect(0, 0, fb_get_width(), fb_get_height());
    desktop_redraw();
}

static void wm_toggle_maximize(window_t* win) {
    if (!win) return;

    // 1. Önce eski konum ve boyutundaki alanı tam kirli işaretle
    damage_union_rect(win->x, win->y, win->width, win->height);

    if (!win->is_maximized) {
        win->prev_x = win->x;
        win->prev_y = win->y;
        win->prev_width = win->width;
        win->prev_height = win->height;

        int screen_w = fb_get_width();
        int screen_h = fb_get_height() - 36; // Alt görev çubuğu payı

        win->x = 0;
        win->y = 0;
        win->width = screen_w;
        win->height = screen_h;
        win->is_maximized = true;
    } else {
        win->x = win->prev_x;
        win->y = win->prev_y;
        win->width = win->prev_width;
        win->height = win->prev_height;
        win->is_maximized = false;
    }

    // --- ÖNEMLİ: Butonlar üzerinde kalan "takılı kalmış" hover durumlarını sıfırla ---
    hovered_max_button = 0;
    hovered_min_button = 0;
    hovered_close_button = 0;

    // 2. İç bileşenlerin referans boyutlarını güncelle
    win->init_win_w = win->width;
    win->init_win_h = win->height;

    for (int i = 0; i < win->panel_count; i++) {
        win->panels[i].init_x = win->panels[i].x;
        win->panels[i].init_y = win->panels[i].y;
        win->panels[i].init_width = win->panels[i].width;
        win->panels[i].init_height = win->panels[i].height;
        win->panels[i].init_win_w = win->width;
        win->panels[i].init_win_h = win->height;
    }
    for (int i = 0; i < win->button_count; i++) {
        win->buttons[i].init_x = win->buttons[i].x;
        win->buttons[i].init_y = win->buttons[i].y;
        win->buttons[i].init_width = win->buttons[i].width;
        win->buttons[i].init_height = win->buttons[i].height;
        win->buttons[i].init_win_w = win->width;
        win->buttons[i].init_win_h = win->height;
    }
    for (int i = 0; i < win->label_count; i++) {
        win->labels[i].init_x = win->labels[i].x;
        win->labels[i].init_y = win->labels[i].y;
        win->labels[i].init_win_w = win->width;
        win->labels[i].init_win_h = win->height;
    }
    for (int i = 0; i < win->combobox_count; i++) {
        win->comboboxes[i].init_x = win->comboboxes[i].x;
        win->comboboxes[i].init_y = win->comboboxes[i].y;
        win->comboboxes[i].init_width = win->comboboxes[i].width;
        win->comboboxes[i].init_height = win->comboboxes[i].height;
        win->comboboxes[i].init_win_w = win->width;
        win->comboboxes[i].init_win_h = win->height;
    }

    // 3. Yeni pencere alanının tamamını kirli işaretle ve ekranı çiz
    damage_union_rect(win->x, win->y, win->width, win->height);
    desktop_redraw();
}

void wm_draw_window(window_t* win) {
    if (!win || win->width == 0) return;

    bool title_hovered = hovered_window == win;
    
    // 3 buton için hover durumları (İsteğe bağlı olarak ayrı takip edebilirsin)
    bool close_hovered = title_hovered && hovered_close_button;

    int header_height = win->has_tabs ? 48 : 24;

    // 1. Window body
    gfx_fill_rect(win->x, win->y, win->width, win->height, win->bg_color);

    // 2. Draw panels (with advanced clipping support)
    for (int i = 0; i < win->panel_count; i++) {
        panel_t* p = &win->panels[i];
        
        int rel_x = p->x;
        int rel_y = header_height + p->y;
        int w = p->width;
        int h = p->height;

        // Left or top edge overflow clipping
        if (rel_x < 0) { w += rel_x; rel_x = 0; }
        if (rel_y < 24) { h += (rel_y - 24); rel_y = 24; }

        // Right or bottom edge overflow clipping
        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + w > win->width) w = win->width - rel_x;
            if (rel_y + h > win->height) h = win->height - rel_y;

            if (w > 0 && h > 0) {
                uint32_t current_color = p->is_hovered ? p->hover_color : p->color;
                gfx_fill_rect(win->x + rel_x, win->y + rel_y, w, h, current_color);
            }
        }
    }

    // 3. Title bar
    uint32_t title_color = title_hovered
        ? (win->is_active ? 0xFF1088D0 : 0xFF666666)
        : (win->is_active ? 0xFF007ACC : 0xFF505050);
    gfx_fill_rect(win->x, win->y, win->width, 24, title_color);

    // 4. Sekmeler varsa başlık çubuğunun İÇİNE sekmeleri, ikonlarını ve '+' butonunu çiz
    if (win->has_tabs) {
        int tab_w = 90;
        int max_visible_w = win->width - 110; // Sağdaki 3'lü buton grubu için pay (3 x ~36px)
        int last_tab_x = win->x + 4;

        for (int i = 0; i < win->tab_count; i++) {
            int tab_x = win->x + 4 + (i * (tab_w + 2));
            last_tab_x = tab_x; 

            // Butonların üzerine taşmayı engelle
            if (tab_x + tab_w > win->x + max_visible_w) break;

            uint32_t tab_bg = (i == win->active_tab_index) ? 0xFF3C3C3C : 0xFF222222;

            // Sekme kutucuğu
            gfx_fill_rounded_rect_advanced(tab_x, win->y + 2, tab_w, 22, 4, 4, 0, 0, tab_bg);

            // Sekme İkonu (Favicon)
            if (win->tabs[i].icon_pixels && win->tabs[i].icon_w > 0) {
                int icon_draw_x = tab_x + 6;
                int icon_draw_y = win->y + 5;
                gfx_draw_buffer(icon_draw_x, icon_draw_y, 14, 14, win->tabs[i].icon_pixels, win->tabs[i].icon_w);
            }

            // Sekme Başlığı
            int text_offset = (win->tabs[i].icon_pixels && win->tabs[i].icon_w > 0) ? 24 : 8;
            gfx_draw_text_utf8(tab_x + text_offset, win->y + 6, 0xFFFFFFFF, win->tabs[i].title);

            // --- SEKME İÇİ KIRMIZI 'x' KAPATMA BUTONU ---
            int close_btn_x = tab_x + tab_w - 18;
            int close_btn_y = win->y + 6;
            gfx_fill_rect(close_btn_x, close_btn_y, 12, 12, 0xFFE81123);
            gfx_draw_text_utf8(close_btn_x + 2, close_btn_y - 1, 0xFFFFFFFF, "x");
        }

        // --- SON SEKMENİN SAĞINA '+' (YENİ SEKME) BUTONU ---
        int plus_btn_x = last_tab_x + tab_w + 6;
        int plus_btn_y = win->y + 4;
        int close_btn_limit = win->x + win->width - 110;

        if (plus_btn_x + 18 < close_btn_limit) {
            gfx_fill_rounded_rect_advanced(plus_btn_x, plus_btn_y, 18, 18, 4, 4, 4, 4, 0xFF2A2A2A);
            gfx_draw_text_utf8(plus_btn_x + 5, plus_btn_y + 1, 0xFF888888, "+");
        }
    } else {
        // Normal pencere başlığı metni
        if (win->width > 120) {
            if (system_font_id >= 0) {
                ttf_set_font_size(system_font_id, 13.0f);
                ttf_draw_text(system_font_id, win->x + 8, win->y + 5, 0xFFFFFFFF, win->title);
            }
        }
    }

    // 5. Sağ Üst Köşede Yan Yana 3 Adet Dikdörtgen Buton
    int btn_width = 36;   
    int btn_height = 24;  
    int total_btns_width = btn_width * 3;
    int btns_start_x = win->x + win->width - total_btns_width;
    int btns_y = win->y;

    if (win->width >= total_btns_width) {
        uint32_t default_btn_bg = title_color; 

        // 5.1. Minimize Butonu (-) - Üzerindeyse beyaz fon, siyah/koyu simge
        uint32_t min_bg = (hovered_window == win && hovered_min_button) ? 0xFFE0E0E0 : default_btn_bg;
        uint32_t min_text_color = (hovered_window == win && hovered_min_button) ? 0xFF000000 : 0xFFFFFFFF;
        
        gfx_fill_rect(btns_start_x, btns_y, btn_width, btn_height, min_bg);
        gfx_draw_text_utf8(btns_start_x + 14, btns_y + 4, min_text_color, "-");

        // 5.2. Maximize Butonu (▢) - Üzerindeyse beyaz fon, siyah/koyu simge
        uint32_t max_bg = (hovered_window == win && hovered_max_button) ? 0xFFE0E0E0 : default_btn_bg;
        uint32_t max_text_color = (hovered_window == win && hovered_max_button) ? 0xFF000000 : 0xFFFFFFFF;

        gfx_fill_rect(btns_start_x + btn_width, btns_y, btn_width, btn_height, max_bg);
        gfx_draw_text_utf8(btns_start_x + btn_width + 13, btns_y + 5, max_text_color, "□");

        // 5.3. Close Butonu (X) - Üzerindeyse kırmızı
        uint32_t close_bg = (hovered_window == win && hovered_close_button) ? 0xFFE81123 : default_btn_bg;
        gfx_fill_rect(btns_start_x + (btn_width * 2), btns_y, btn_width, btn_height, close_bg);
        gfx_draw_text_utf8(btns_start_x + (btn_width * 2) + 13, btns_y + 4, 0xFFFFFFFF, "X");
    }

    // 6. Draw labels
    for (int i = 0; i < win->label_count; i++) {
        label_item_t* l = &win->labels[i];
        
        int screen_x = win->x + l->x;
        int screen_y = win->y + WM_TITLEBAR_HEIGHT + l->y; 

        // Eğer bu etiket seçilebilirse ve seçilmiş bir aralık varsa arkasına mavi vurgu çiz
        if (l->selectable && l->sel_start != -1 && l->sel_end != -1 && l->sel_start != l->sel_end) {
            int s = l->sel_start;
            int e = l->sel_end;
            if (s > e) { int tmp = s; s = e; e = tmp; } // Ters seçim koruması

            int char_w = 8;
            int highlight_x = screen_x + (s * char_w);
            int highlight_w = (e - s) * char_w;
            int highlight_h = (l->font_size > 0) ? l->font_size + 2 : 16;

            gfx_fill_rect(highlight_x, screen_y, highlight_w, highlight_h, 0xFF3399FF); // Mavi vurgu rengi
        }

        if (system_font_id >= 0) {
            float fsize = (l->font_size > 0) ? (float)l->font_size : 14.0f;
            ttf_set_font_size(system_font_id, fsize);
            
            ttf_draw_text(system_font_id, screen_x, screen_y, l->color, l->text);
        }
    }

    // 6.5. Draw browser link underlines
    for (int i = 0; i < win->link_count; i++) {
        browser_link_t* lnk = &win->links[i];
        int rel_x = lnk->x;
        int rel_y = 24 + lnk->y + 16;
        int lw = lnk->width;

        if (rel_x >= 0 && rel_x < win->width && rel_y >= 24 && rel_y < win->height) {
            if (rel_x + lw > win->width) {
                lw = win->width - rel_x;
            }
            if (lw > 0) {
                gfx_fill_rect(win->x + rel_x, win->y + rel_y, lw, 1, 0xFF007ACC);
            }
        }
    }

    // 7. Draw buttons
    for (int i = 0; i < win->button_count; i++) {
        button_t* b = &win->buttons[i];
        int rel_x = b->x;
        int rel_y = 24 + b->y;
        int bw = b->width;
        int bh = b->height;

        if (rel_x < 0) { bw += rel_x; rel_x = 0; }
        if (rel_y < 24) { bh += (rel_y - 24); rel_y = 24; }

        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + bw > win->width) bw = win->width - rel_x;
            if (rel_y + bh > win->height) bh = win->height - rel_y;

            if (bw > 0 && bh > 0) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;
                
                uint32_t current_bg_color = b->is_hovered ? (b->hover_color != 0 ? b->hover_color : 0xFF505050) : b->bg_color;
                
                gfx_fill_rect(abs_x, abs_y, bw, bh, current_bg_color);
                
                if (bw > 10) {
                    gfx_draw_text_utf8(abs_x + 8, abs_y + 8, b->text_color, b->text);
                }
            }
        }
    }

    // 7.5. Draw Canvas (3D Oyun / Piksel Tamponu)
    if (win->has_canvas && win->canvas_buffer) {
        int rel_x = win->canvas_x;
        int rel_y = 24 + win->canvas_y;
        int cw = win->canvas_w;
        int ch = win->canvas_h;

        if (rel_x < 0) { cw += rel_x; rel_x = 0; }
        if (rel_y < 24) { ch += (rel_y - 24); rel_y = 24; }

        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + cw > win->width) cw = win->width - rel_x;
            if (rel_y + ch > win->height) ch = win->height - rel_y;

            if (cw > 0 && ch > 0) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;
                gfx_draw_buffer(abs_x, abs_y, cw, ch, win->canvas_buffer, win->canvas_w);
            }
        }
    }

    // 8. Draw pictureboxes
    for (int i = 0; i < win->picturebox_count; i++) {
        picturebox_t* p = &win->pictureboxes[i];
        int rel_x = p->x;
        int rel_y = 24 + p->y;
        int pw = p->width;
        int ph = p->height;

        int src_x = 0;
        int src_y = 0;

        if (rel_x < 0) { src_x = -rel_x; pw += rel_x; rel_x = 0; }
        if (rel_y < 24) { src_y = 24 - rel_y; ph += (rel_y - 24); rel_y = 24; }

        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + pw > win->width) pw = win->width - rel_x;
            if (rel_y + ph > win->height) ph = win->height - rel_y;

            if (pw > 0 && ph > 0 && p->pixels) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;
                uint32_t* src_pixels = p->pixels + (src_y * p->img_width + src_x);
                gfx_draw_buffer(abs_x, abs_y, pw, ph, src_pixels, p->img_width);
            }
        }
    }

    // 9. Draw text inputs
    for (int i = 0; i < win->input_count; i++) {
        text_input_t* inp = &win->inputs[i];
        int rel_x = inp->x;
        int rel_y = 24 + inp->y;
        int iw = inp->width;
        int ih = inp->height;

        if (rel_x < 0) { iw += rel_x; rel_x = 0; }
        if (rel_y < 24) { ih += (rel_y - 24); rel_y = 24; }

        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + iw > win->width) iw = win->width - rel_x;
            if (rel_y + ih > win->height) ih = win->height - rel_y;

            if (iw > 0 && ih > 0) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;

                gfx_fill_rect(abs_x, abs_y, iw, ih, inp->bg_color);

                if (inp->border_thickness > 0) {
                    gfx_fill_rect(abs_x, abs_y, iw, inp->border_thickness, inp->border_color);
                    gfx_fill_rect(abs_x, abs_y + ih - inp->border_thickness, iw, inp->border_thickness, inp->border_color);
                    gfx_fill_rect(abs_x, abs_y, inp->border_thickness, ih, inp->border_color);
                    gfx_fill_rect(abs_x + iw - inp->border_thickness, abs_y, inp->border_thickness, ih, inp->border_color);
                }

                if (inp->text[0] != '\0') {
                    int current_y = abs_y + 6;
                    int line_height = 16; // Her satır arasındaki piksel mesafesi
                    char line_buf[128];
                    int l_idx = 0;

                    for (int k = 0; inp->text[k] != '\0'; k++) {
                        if (inp->text[k] == '\n') {
                            line_buf[l_idx] = '\0';
                            gfx_draw_text_utf8(abs_x + 6, current_y, inp->text_color, line_buf);
                            current_y += line_height;
                            l_idx = 0;
                        } else {
                            if (l_idx < 127) {
                                line_buf[l_idx++] = inp->text[k];
                            }
                        }
                    }
                    // Son satırı çiz
                    line_buf[l_idx] = '\0';
                    gfx_draw_text_utf8(abs_x + 6, current_y, inp->text_color, line_buf);

                } else if (inp->placeholder[0] != '\0') {
                    gfx_draw_text_utf8(abs_x + 6, abs_y + 6, inp->placeholder_color, inp->placeholder);
                }

                if (active_input == inp) {
                    int text_len = 0;
                    while(inp->text[text_len] != '\0') text_len++;
                    int cursor_pos_x = abs_x + 6 + (text_len * 8);
                    if (cursor_pos_x < abs_x + iw - 8) {
                        gfx_fill_rect(cursor_pos_x, abs_y + 6, 2, ih - 12, inp->text_color);
                    }
                }
            }
        }
    }

    // 10. Draw comboboxes
    for (int i = 0; i < win->combobox_count; i++) {
        combobox_t* cb = &win->comboboxes[i];
        int rel_x = cb->x;
        int rel_y = 24 + cb->y;
        int cw = cb->width;
        int ch = cb->height;

        if (rel_x < 0) { cw += rel_x; rel_x = 0; }
        if (rel_y < 24) { ch += (rel_y - 24); rel_y = 24; }

        if (rel_x < win->width && rel_y < win->height) {
            if (rel_x + cw > win->width) cw = win->width - rel_x;
            if (rel_y + ch > win->height) ch = win->height - rel_y;

            if (cw > 0 && ch > 0) {
                int abs_x = win->x + rel_x;
                int abs_y = win->y + rel_y;

                gfx_fill_rect(abs_x, abs_y, cw, ch, cb->bg_color);
                gfx_fill_rect(abs_x, abs_y, cw, 1, cb->border_color);
                gfx_fill_rect(abs_x, abs_y + ch - 1, cw, 1, cb->border_color);
                gfx_fill_rect(abs_x, abs_y, 1, ch, cb->border_color);
                gfx_fill_rect(abs_x + cw - 1, abs_y, 1, ch, cb->border_color);

                if (cb->items && cb->selected_index >= 0 && cb->selected_index < cb->item_count) {
                    gfx_draw_text_utf8(abs_x + 6, abs_y + 6, cb->text_color, cb->items[cb->selected_index]);
                }

                gfx_draw_text_utf8(abs_x + cw - 16, abs_y + 6, cb->text_color, "v");

                if (cb->is_open && cb->item_count > 0) {
                    int item_height = 20;
                    int list_height = cb->item_count * item_height;
                    int list_y = abs_y + ch;

                    gfx_fill_rect(abs_x, list_y, cw, list_height, 0xFFFFFFFF);
                    gfx_fill_rect(abs_x, list_y, cw, list_height, cb->border_color);

                    for (int j = 0; j < cb->item_count; j++) {
                        int cur_item_y = list_y + (j * item_height);
                        if (j == cb->selected_index) {
                            gfx_fill_rect(abs_x + 1, cur_item_y, cw - 2, item_height, 0xFF007ACC);
                            gfx_draw_text_utf8(abs_x + 6, cur_item_y + 4, 0xFFFFFFFF, cb->items[j]);
                        } else {
                            gfx_draw_text_utf8(abs_x + 6, cur_item_y + 4, 0xFF000000, cb->items[j]);
                        }
                    }
                }
            }
        }
    }
}

void wm_draw_all(void) {
    for (int i = 0; i < window_count; i++) {
        if (window_list[i].width > 0) {
            wm_draw_window(&window_list[i]);
        }
    }
}

window_t* wm_find_at(int x, int y) {
    for (int i = window_count - 1; i >= 0; i--) {
        window_t* win = &window_list[i];
        if (win->width > 0 &&
            x >= win->x && x <= win->x + win->width &&
            y >= win->y && y <= win->y + win->height) {
            return win;
        }
    }
    return 0;
}

static int get_resize_direction(window_t* win, int x, int y) {
    if (!win) return RESIZE_NONE;

    int dir = RESIZE_NONE;
    int left = win->x;
    int right = win->x + win->width;
    int top = win->y;
    int bottom = win->y + win->height;

    // Tolerance for edge and corner detection
    if (x >= left - RESIZE_BORDER && x <= right + RESIZE_BORDER &&
        y >= top - RESIZE_BORDER && y <= bottom + RESIZE_BORDER) {
        
        if (x < left + RESIZE_BORDER) dir |= RESIZE_LEFT;
        else if (x > right - RESIZE_BORDER) dir |= RESIZE_RIGHT;

        if (y < top + RESIZE_BORDER) dir |= RESIZE_TOP;
        else if (y > bottom - RESIZE_BORDER) dir |= RESIZE_BOTTOM;
    }

    return dir;
}

static window_t* wm_bring_to_front(window_t* win) {
    if (!win || window_count <= 1) return win;

    // Eski aktif pencerenin başlık çubuğunu ve gövdesini kirli işaretle
    if (active_window && active_window != win) {
        damage_union_rect(active_window->x, active_window->y, active_window->width, active_window->height);
    }

    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (&window_list[i] == win) {
            idx = i;
            break;
        }
    }

    if (idx == -1) return win;

    if (idx == window_count - 1) {
        win->is_active = true;
        active_window = win;
        damage_union_rect(win->x, win->y, win->width, win->height); // Yeni pencereyi kirli işaretle
        return win;
    }

    window_t temp = window_list[idx];
    for (int i = idx; i < window_count - 1; i++) {
        window_list[i] = window_list[i + 1];
    }
    window_list[window_count - 1] = temp;

    for (int i = 0; i < window_count; i++) {
        if (window_list[i].width > 0) {
            window_list[i].is_active = (i == window_count - 1);
        }
    }
    
    active_window = &window_list[window_count - 1];
    
    // Öne gelen yeni pencerenin alanını tamamen (başlık çubuğu dahil) yeniden çizime dahil et
    damage_union_rect(active_window->x, active_window->y, active_window->width, active_window->height);
    return active_window;
}

static void wm_update_hover_state(void) {
    // Yerel değişken yerine global/static olan hovered_window değişkenini güncelliyoruz
    hovered_window = wm_find_at(mouse_x, mouse_y);

    for (int i = 0; i < window_count; i++) {
        window_t* win = &window_list[i];
        if (!win || win->width == 0) continue;

        // --- 3'lü Buton Grubu Kapatma (Close) Butonu Hover Kontrolü ---
        // hovered_window artık doğru şekilde güncellendiği için burası çalışacak
        if (win == hovered_window && win->is_active) {
            int btn_width = 36;
            int btn_height = 24;
            int total_btns_width = btn_width * 3;
            int btns_start_x = win->x + win->width - total_btns_width;
            int btns_y = win->y;
            
            int min_btn_x = btns_start_x;                  // 1. Buton: Minimize
            int max_btn_x = btns_start_x + btn_width;      // 2. Buton: Maximize
            int close_btn_x = btns_start_x + (btn_width * 2); // 3. Buton: Close

            // --- Minimize Butonu Hover Kontrolü ---
            if (mouse_x >= min_btn_x && mouse_x < min_btn_x + btn_width &&
                mouse_y >= btns_y && mouse_y < btns_y + btn_height) {
                if (!hovered_min_button) {
                    hovered_min_button = 1;
                    damage_union_rect(min_btn_x, btns_y, btn_width, btn_height);
                }
            } else {
                if (hovered_min_button) {
                    hovered_min_button = 0;
                    damage_union_rect(min_btn_x, btns_y, btn_width, btn_height);
                }
            }

            // --- Maximize Butonu Hover Kontrolü ---
            if (mouse_x >= max_btn_x && mouse_x < max_btn_x + btn_width &&
                mouse_y >= btns_y && mouse_y < btns_y + btn_height) {
                if (!hovered_max_button) {
                    hovered_max_button = 1;
                    damage_union_rect(max_btn_x, btns_y, btn_width, btn_height);
                }
            } else {
                if (hovered_max_button) {
                    hovered_max_button = 0;
                    damage_union_rect(max_btn_x, btns_y, btn_width, btn_height);
                }
            }

            // --- Close Butonu Hover Kontrolü (Zaten vardı) ---
            if (mouse_x >= close_btn_x && mouse_x < close_btn_x + btn_width &&
                mouse_y >= btns_y && mouse_y < btns_y + btn_height) {
                if (!hovered_close_button) {
                    hovered_close_button = 1;
                    damage_union_rect(close_btn_x, btns_y, btn_width, btn_height);
                }
            } else {
                if (hovered_close_button) {
                    hovered_close_button = 0;
                    damage_union_rect(close_btn_x, btns_y, btn_width, btn_height);
                }
            }
        }

        // --- 1. BUTONLAR İÇİN HOVER KONTROLÜ ---
        for (int b = 0; b < win->button_count; b++) {
            button_t* btn = &win->buttons[b];
            
            int abs_x = win->x + btn->x;
            int abs_y = win->y + 24 + btn->y;

            if (mouse_x >= abs_x && mouse_x < abs_x + btn->width &&
                mouse_y >= abs_y && mouse_y < abs_y + btn->height) {
                
                if (!btn->is_hovered) {
                    btn->is_hovered = true;
                    damage_union_rect(abs_x, abs_y, btn->width, btn->height);
                }
            } else {
                if (btn->is_hovered) {
                    btn->is_hovered = false;
                    damage_union_rect(abs_x, abs_y, btn->width, btn->height);
                }
            }
        }

        // --- 2. PANELLER İÇİN HOVER KONTROLÜ ---
        for (int p_idx = 0; p_idx < win->panel_count; p_idx++) {
            panel_t* panel = &win->panels[p_idx];

            if (panel->hover_color == 0 && panel->on_hover == NULL) {
                continue;
            }

            int abs_x = win->x + panel->x;
            int abs_y = win->y + 24 + panel->y;

            if (mouse_x >= abs_x && mouse_x < abs_x + panel->width &&
                mouse_y >= abs_y && mouse_y < abs_y + panel->height) {
                
                if (!panel->is_hovered) {
                    panel->is_hovered = true;
                    damage_union_rect(abs_x, abs_y, panel->width, panel->height);
                }
            } else {
                if (panel->is_hovered) {
                    panel->is_hovered = false;
                    damage_union_rect(abs_x, abs_y, panel->width, panel->height);
                }
            }
        }

        // --- 3. COMBOBOX AÇILIR LİSTE HOVER KONTROLÜ ---
        if (win->is_active) {
            for (int c = 0; c < win->combobox_count; c++) {
                combobox_t* cb = &win->comboboxes[c];
                if (!cb->is_open || cb->item_count <= 0) continue;

                int abs_x = win->x + cb->x;
                int abs_y = win->y + 24 + cb->y;
                int item_height = 20;
                int list_height = cb->item_count * item_height;
                int list_y = abs_y + cb->height;

                if (mouse_x >= abs_x && mouse_x < abs_x + cb->width &&
                    mouse_y >= list_y && mouse_y < list_y + list_height) {
                    damage_union_rect(abs_x, list_y, cb->width, list_height);
                }
            }
        }
    }
}

static void wm_update_cursor(void) {
    window_t* win = wm_get_active_window();
    if (!win || win->width == 0) {
        cursor_set_type(CURSOR_ARROW);
        return;
    }

    cursor_type_t new_cursor = CURSOR_ARROW;

    // 1. Text Input alanlarını kontrol et (I-Beam / Metin İmleci)
    for (int i = 0; i < win->input_count; i++) {
        text_input_t* inp = &win->inputs[i];
        int abs_x = win->x + inp->x;
        int abs_y = win->y + 24 + inp->y;

        if (mouse_x >= abs_x && mouse_x < abs_x + inp->width &&
            mouse_y >= abs_y && mouse_y < abs_y + inp->height) {
            new_cursor = CURSOR_IBEAM;
            break;
        }
    }

    // 2. Input üzerinde değilse butonları kontrol et (Hand / El İmleci)
    if (new_cursor == CURSOR_ARROW) {
        for (int i = 0; i < win->button_count; i++) {
            button_t* btn = &win->buttons[i];
            int abs_x = win->x + btn->x;
            int abs_y = win->y + 24 + btn->y;

            if (mouse_x >= abs_x && mouse_x < abs_x + btn->width &&
                mouse_y >= abs_y && mouse_y < abs_y + btn->height) {
                new_cursor = CURSOR_HAND;
                break;
            }
        }
    }

    // 3. Tarayıcı linklerini (<a> etiketleri) kontrol et (Hand / El İmleci)
    if (new_cursor == CURSOR_ARROW) {
        for (int i = 0; i < win->link_count; i++) {
            browser_link_t* lnk = &win->links[i];
            
            int abs_x = win->x + lnk->x;
            int abs_y = win->y + 24 + lnk->y;
            int abs_w = lnk->width;
            int abs_h = lnk->height > 0 ? lnk->height : 18;

            if (mouse_x >= abs_x && mouse_x < abs_x + abs_w &&
                mouse_y >= abs_y && mouse_y < abs_y + abs_h) {
                new_cursor = CURSOR_HAND;
                break;
            }
        }
    }
   
    // 4. Seçilebilir HTML etiketlerini kontrol et (I-Beam / Metin İmleci)
    if (new_cursor == CURSOR_ARROW) {
        for (int i = 0; i < win->label_count; i++) {
            label_item_t* l = &win->labels[i];
            if (!l->selectable) continue;

            int abs_x = win->x + l->x;
            int abs_y = win->y + WM_TITLEBAR_HEIGHT + l->y;
            int char_w = 8; // Ortalama karakter genişliği
            int text_w = strlen(l->text) * char_w;
            int text_h = (l->font_size > 0) ? l->font_size + 4 : 18;

            if (mouse_x >= abs_x && mouse_x < abs_x + text_w &&
                mouse_y >= abs_y && mouse_y < abs_y + text_h) {
                new_cursor = CURSOR_IBEAM;
                break;
            }
        }
    }

    // İmleç türünü sistem genelinde güncelle
    cursor_set_type(new_cursor);
}

void wm_process_input(void) {
    global_wm_ticks++;

    wm_update_hover_state();
    wm_update_cursor();

    // --- KLAVYE ENTER (0x1C) KONTROLÜ ---
    uint8_t scancode = keyboard_get_last_scancode();
    if (scancode == 0x1C) { // Enter Tuşu
        if (active_input) {
            int len = 0;
            while (active_input->text[len] != '\0') len++;

            // Buffer sınırları içinde yeni satır (\n) karakteri ekle
            if (len < 126) {
                active_input->text[len] = '\n';
                active_input->text[len + 1] = '\0';

                // Ekranı tazele ve yeniden çiz
                damage_union_rect(0, 0, fb_get_width(), fb_get_height());
                desktop_redraw();
            }
        }
        keyboard_clear_last_scancode(); // Tuşun tekrar tetiklenmesini önle
    }
    // ----------------------------------

    uint8_t left_pressed = mouse_buttons & 0x01;
    uint8_t prev_left = prev_buttons & 0x01;

    // 1. If the left button was released, dragging or resizing must end.
    if (!left_pressed) {
        // --- TIKLANAN LİNK RENGİNİ ESKİ HALİNE DÖNDÜR ---
        // Sol tık bırakıldığında mor/lita yapılan linkleri tekrar orijinal mavi rengine (0xFF007ACC) çevir    
        for (int i = 0; i < window_count; i++) {
            window_t* win = &window_list[i];
            if (win->width > 0) {
                for (int l_idx = 0; l_idx < win->label_count; l_idx++) {
                    if (win->labels[l_idx].color == 0xFF9400D3) {
                        win->labels[l_idx].color = 0xFF007ACC;
                        damage_union_rect(win->x, win->y, win->width, win->height);
                        desktop_redraw();
                    }
                }
            }
        }
        // ------------------------------------------------

        if (dragged_window || resized_window) {
            cursor_prepare_redraw();
            if (dragged_window) {
                damage_union_rect(dragged_window->x, dragged_window->y, dragged_window->width, dragged_window->height);
                dragged_window->is_dragging = false;
                dragged_window = 0;
            }
            if (resized_window) {
                // RESIZING ENDED: Lock reference (init) values to the new size!
                resized_window->init_win_w = resized_window->width;
                resized_window->init_win_h = resized_window->height;

                for (int i = 0; i < resized_window->panel_count; i++) {
                    resized_window->panels[i].init_x = resized_window->panels[i].x;
                    resized_window->panels[i].init_y = resized_window->panels[i].y;
                    resized_window->panels[i].init_width = resized_window->panels[i].width;
                    resized_window->panels[i].init_height = resized_window->panels[i].height;
                    resized_window->panels[i].init_win_w = resized_window->width;
                    resized_window->panels[i].init_win_h = resized_window->height;
                }
                for (int i = 0; i < resized_window->button_count; i++) {
                    resized_window->buttons[i].init_x = resized_window->buttons[i].x;
                    resized_window->buttons[i].init_y = resized_window->buttons[i].y;
                    resized_window->buttons[i].init_width = resized_window->buttons[i].width;
                    resized_window->buttons[i].init_height = resized_window->buttons[i].height;
                    resized_window->buttons[i].init_win_w = resized_window->width;
                    resized_window->buttons[i].init_win_h = resized_window->height;
                }
                for (int i = 0; i < resized_window->label_count; i++) {
                    resized_window->labels[i].init_x = resized_window->labels[i].x;
                    resized_window->labels[i].init_y = resized_window->labels[i].y;
                    resized_window->labels[i].init_win_w = resized_window->width;
                    resized_window->labels[i].init_win_h = resized_window->height;
                }
                for (int i = 0; i < resized_window->combobox_count; i++) {
                    resized_window->comboboxes[i].init_x = resized_window->comboboxes[i].x;
                    resized_window->comboboxes[i].init_y = resized_window->comboboxes[i].y;
                    resized_window->comboboxes[i].init_width = resized_window->comboboxes[i].width;
                    resized_window->comboboxes[i].init_height = resized_window->comboboxes[i].height;
                    resized_window->comboboxes[i].init_win_w = resized_window->width;
                    resized_window->comboboxes[i].init_win_h = resized_window->height;
                }

                damage_union_rect(resize_old_x, resize_old_y, resize_old_w, resize_old_h);
                damage_union_rect(resized_window->x, resized_window->y, resized_window->width, resized_window->height);
                
                resized_window = 0;
                resize_direction = RESIZE_NONE;
            }
            desktop_redraw();
            cursor_sync_position();
            cursor_show();
        }
        prev_buttons = mouse_buttons;
        return;
    }

    // 2. If the left button is pressed and was just clicked (click/focus/drag start)
    if (left_pressed && !prev_left) {
        window_t* target = wm_find_at(mouse_x, mouse_y);
        if (target) {
            window_t* front_win = wm_bring_to_front(target);

            // İŞTE BURAYA EKLENECEK: Tıklanan pencerenin başlık çubuğu ve gövdesini tazele
            damage_union_rect(target->x, target->y, target->width, target->height);

            int btn_width = 36;
            int btn_height = 24;
            int total_btns_width = btn_width * 3;
            int btns_start_x = target->x + target->width - total_btns_width;
            int close_btn_x = btns_start_x + (btn_width * 2);
            int btn_y = target->y;

            cursor_prepare_redraw();

            // [X] Close button (Sağdaki 3. buton)
            if (target->width >= total_btns_width &&
                mouse_x >= close_btn_x && mouse_x < close_btn_x + btn_width &&
                mouse_y >= btn_y && mouse_y < btn_y + btn_height) {
                wm_close_window(target);
                cursor_sync_position();
                cursor_show();
                prev_buttons = mouse_buttons;
                return;
            }

            // --- IN-WINDOW BUTTON CLICK CHECK ---
            bool clicked_on_button = false;
            for (int i = 0; i < target->button_count; i++) {
                button_t* b = &target->buttons[i]; 
                int abs_bx = target->x + b->x;
                int abs_by = target->y + 24 + b->y;

                if (mouse_x >= abs_bx && mouse_x < abs_bx + b->width &&
                    mouse_y >= abs_by && mouse_y < abs_by + b->height) {
                    
                    serial_write("WM: In-window button clicked: ");
                    serial_write(b->text);
                    serial_write("\n");

                    if (b->on_click != 0) {
                        b->on_click();
                    }

                    clicked_on_button = true;
                    break;
                }
            }

            // --- IN-WINDOW TEXT INPUT FOCUS CHECK ---
            bool clicked_on_input = false;
            for (int i = 0; i < target->input_count; i++) {
                text_input_t* inp = &target->inputs[i];
                int abs_ix = target->x + inp->x;
                int abs_iy = target->y + 24 + inp->y;

                if (mouse_x >= abs_ix && mouse_x < abs_ix + inp->width &&
                    mouse_y >= abs_iy && mouse_y < abs_iy + inp->height) {
                    
                    active_input = inp;
                    clicked_on_input = true;
                    serial_write("WM: Text input focused\n");
                    break;
                }
            }

            // --- COMBOBOX CLICK CHECK ---
            bool clicked_on_combobox = false;
            for (int i = 0; i < target->combobox_count; i++) {
                combobox_t* cb = &target->comboboxes[i];
                int abs_cx = target->x + cb->x;
                int abs_cy = target->y + 24 + cb->y;

                // 1. Ana combobox kutusuna tıklandı mı?
                if (mouse_x >= abs_cx && mouse_x < abs_cx + cb->width &&
                    mouse_y >= abs_cy && mouse_y < abs_cy + cb->height) {
                    
                    // Diğer açık combobox'ları kapat
                    for (int k = 0; k < target->combobox_count; k++) {
                        if (k != i) target->comboboxes[k].is_open = false;
                    }

                    cb->is_open = !cb->is_open;
                    clicked_on_combobox = true;
                    
                    // BURASI EKLENDİ: Menü açılıp/kapanırken ekranı tazele
                    damage_union_rect(0, 0, fb_get_width(), fb_get_height());
                    desktop_redraw();

                    serial_write("WM: Combobox clicked\n");
                    break;
                }

                // 2. Menü açıksa ve açılan liste elemanlarına tıklandı mı?
                if (cb->is_open && cb->item_count > 0) {
                    int item_height = 20;
                    int list_height = cb->item_count * item_height;
                    int list_y = abs_cy + cb->height;

                    if (mouse_x >= abs_cx && mouse_x < abs_cx + cb->width &&
                        mouse_y >= list_y && mouse_y < list_y + list_height) {
                        
                        int clicked_index = (mouse_y - list_y) / item_height;
                        if (clicked_index >= 0 && clicked_index < cb->item_count) {
                            cb->selected_index = clicked_index;
                            cb->is_open = false;
                            
                            damage_union_rect(0, 0, fb_get_width(), fb_get_height());
                            desktop_redraw();

                            serial_write("WM: Combobox item selected\n");
                        }
                        clicked_on_combobox = true;
                        break;
                    }
                }
            }

            // Eğer combobox dışına tıklandıysa açık olanları kapat
            if (!clicked_on_combobox) {
                for (int k = 0; k < target->combobox_count; k++) {
                    target->comboboxes[k].is_open = false;
                }
            }

            // Eğer ne bir butona, ne bir inputa ne de bir combobox'a tıklandıysa aktif inputu sıfırla
            if (!clicked_on_input && !clicked_on_button && !clicked_on_combobox) {
                active_input = 0; 
            }

            selecting_label = 0;
            for (int i = 0; i < target->label_count; i++) {
                target->labels[i].sel_start = -1;
                target->labels[i].sel_end = -1;
            }

            for (int i = 0; i < target->label_count; i++) {
                label_item_t* l = &target->labels[i];
                if (!l->selectable) continue;

                int abs_x = target->x + l->x;
                int abs_y = target->y + WM_TITLEBAR_HEIGHT + l->y;
                int char_w = 8; // Ortalama karakter genişliği
                int text_w = strlen(l->text) * char_w;
                int text_h = (l->font_size > 0) ? l->font_size + 4 : 18;

                if (mouse_x >= abs_x && mouse_x <= abs_x + text_w &&
                    mouse_y >= abs_y && mouse_y <= abs_y + text_h) {
                    
                    int char_idx = (mouse_x - abs_x) / char_w;
                    int len = strlen(l->text);
                    if (char_idx > len) char_idx = len;

                    selecting_label = l;
                    l->sel_start = char_idx;
                    l->sel_end = char_idx;
                    break;
                }
            }

            // --- TARAYICI LİNK TIKLAMA KONTROLÜ ---
            bool clicked_on_link = false;
            if (target->link_count > 0) {
                for (int i = 0; i < target->link_count; i++) {
                    browser_link_t* lnk = &target->links[i];

                    int abs_lx = target->x + lnk->x;
                    int abs_ly = target->y + WM_TITLEBAR_HEIGHT + lnk->y;
                    int lw = lnk->width > 0 ? lnk->width : 100;
                    int lh = lnk->height > 0 ? lnk->height : 18;

                    if (mouse_x >= abs_lx && mouse_x < abs_lx + lw &&
                        mouse_y >= abs_ly && mouse_y < abs_ly + lh) {
                        
                        serial_write("WM: Link tiklandi -> ");
                        serial_write(lnk->href);
                        serial_write("\n");

                        // Tiklanan linke ait metni ve etiket rengini güncelle (Örn: Mor/Lila renk: 0xFF9400D3)
                        for (int l_idx = 0; l_idx < target->label_count; l_idx++) {
                            if (strcmp(target->labels[l_idx].text, lnk->text) == 0) {
                                target->labels[l_idx].color = 0xFF9400D3;
                            }
                        }

                        clicked_on_link = true;
                        break;
                    }
                }
            }

            // --- SEKME TIKLAMA KONTROLÜ ---
            bool clicked_on_tab = false;
            if (target->has_tabs) {
                int tab_bar_y = target->y + 24;
                if (mouse_y >= tab_bar_y && mouse_y < tab_bar_y + 24) {
                    int rel_x = mouse_x - (target->x + 4);
                    int tab_w = 92;
                    int clicked_idx = rel_x / tab_w;

                    if (clicked_idx >= 0 && clicked_idx < target->tab_count) {
                        target->active_tab_index = clicked_idx;
                        clicked_on_tab = true;
                        damage_union_rect(0, 0, fb_get_width(), fb_get_height());
                        desktop_redraw();
                    }
                }
            }

            // Resize check
            int dir = !clicked_on_tab ? get_resize_direction(target, mouse_x, mouse_y) : RESIZE_NONE;
            if (dir != RESIZE_NONE) {
                resized_window = target;
                resize_direction = dir;
                
                resize_old_x = target->x;
                resize_old_y = target->y;
                resize_old_w = target->width;
                resize_old_h = target->height;
            } else if (!clicked_on_button && !clicked_on_input && !clicked_on_combobox) {
                if (mouse_y >= target->y && mouse_y < target->y + WM_TITLEBAR_HEIGHT) {
                    bool clicked_on_tab = false;

                    // Eğer pencerede sekmeler varsa başlık çubuğu ve sekme tıklamalarını kontrol et
                    if (target->has_tabs) {
                        int tab_w = 90;
                        int max_visible_w = target->width - 30;
                        int last_tab_x = target->x + 4;

                        // Son sekmenin X koordinatını buluyoruz (çizim mantığıyla birebir aynı)
                        for (int i = 0; i < target->tab_count; i++) {
                            int tab_x = target->x + 4 + (i * (tab_w + 2));
                            last_tab_x = tab_x;
                            if (tab_x + tab_w > target->x + max_visible_w) break;
                        }

                        int plus_btn_x = last_tab_x + tab_w + 6;
                        int plus_btn_y = target->y + 4;
                        int close_btn_limit = target->x + target->width - 26;

                        // 1. ÖNCE '+' BUTONUNA TIKLANDI MI KONTROL ET
                        if (plus_btn_x + 18 < close_btn_limit &&
                            mouse_x >= plus_btn_x && mouse_x <= plus_btn_x + 18 &&
                            mouse_y >= plus_btn_y && mouse_y <= plus_btn_y + 18) {
                            
                            // Maksimum sekme sınırını aşmıyorsa yeni sekme ekle
                            if (target->tab_count < MAX_TABS) {
                                int new_idx = target->tab_count;
                                target->tab_count++;
                                target->active_tab_index = new_idx;

                                // Yeni sekmenin varsayılan özellikleri
                                snprintf(target->tabs[new_idx].title, sizeof(target->tabs[new_idx].title), "Sekme %d", new_idx + 1);
                                target->tabs[new_idx].url[0] = '\0';
                                target->tabs[new_idx].icon_pixels = 0;
                                target->tabs[new_idx].icon_w = 0;
                                target->tabs[new_idx].icon_h = 0;

                                clicked_on_tab = true;
                                damage_union_rect(target->x, target->y, target->width, target->height);
                                desktop_redraw();
                            }
                        } 
                        else {
                            // 2. MEVCUT SEKMELERE VE KIRMIZI 'x' BUTONLARINA TIKLANDI MI KONTROL ET
                            int rel_x = mouse_x - (target->x + 4);
                            int tab_stride = tab_w + 2; // Sekme genişliği + boşluk (92px)
                            int clicked_idx = rel_x / tab_stride;

                            if (clicked_idx >= 0 && clicked_idx < target->tab_count) {
                                int tab_start_x = target->x + 4 + (clicked_idx * tab_stride);
                                
                                // Tıklanan nokta aradaki 2 piksellik boşluğa değil, doğrudan sekmenin üstüne mi?
                                if (mouse_x >= tab_start_x && mouse_x < tab_start_x + tab_w) {
                                    // Sekme içindeki kırmızı 'x' butonunun koordinatları
                                    int close_btn_x = tab_start_x + tab_w - 18;
                                    int close_btn_y = target->y + 6;

                                    if (mouse_x >= close_btn_x && mouse_x <= close_btn_x + 12 &&
                                        mouse_y >= close_btn_y && mouse_y <= close_btn_y + 12) {
                                        // Kırmızı x'e tıklandı, sekmesini kapat!
                                        wm_close_tab(target, clicked_idx);
                                    } else {
                                        // Normal sekme alanına tıklandı, aktif sekmeyi değiştir
                                        target->active_tab_index = clicked_idx;
                                    }

                                    clicked_on_tab = true;
                                    damage_union_rect(target->x, target->y, target->width, target->height);
                                    desktop_redraw();
                                }
                            }
                        }
                    }

                    // Bir sekmenin veya '+' butonunun üzerine basılmadıysa sürükleme veya çift tık kontrolü yap
                    if (!clicked_on_tab) {
                        int btn_width = 36;
                        int total_btns_width = btn_width * 3;
                        int btns_start_x = target->x + target->width - total_btns_width;
                        int max_btn_x = btns_start_x + btn_width;

                        // 1. Maximize butonuna (□) tıklandıysa direkt tam ekran yap / küçült
                        if (mouse_x >= max_btn_x && mouse_x < max_btn_x + btn_width &&
                            mouse_y >= target->y && mouse_y < target->y + 24) {
                            wm_toggle_maximize(target);
                        } 
                        else {
                            // 2. Başlık çubuğuna çift tıklama kontrolü (Örn: 25 tick eşiği)
                            if (last_clicked_win == target && (global_wm_ticks - last_click_time < 25)) {
                                // ÇİFT TIKLAMA TESPİT EDİLDİ!
                                wm_toggle_maximize(target);
                                last_clicked_win = 0;
                                last_click_time = 0;
                            } else {
                                // İlk tıklama
                                last_clicked_win = target;
                                last_click_time = global_wm_ticks;
                                
                                // Normal sürükleme (drag) işlemini başlat
                                dragged_window = front_win;
                                dragged_window->is_dragging = true;
                                dragged_window->drag_offset_x = mouse_x - target->x;
                                dragged_window->drag_offset_y = mouse_y - target->y;
                            }
                        }
                    }
                }
            }

            desktop_redraw();
            cursor_sync_position();
            cursor_show();
        } 
        else {
            // Pencere dışına (masaüstüne) tıklandığında aktif inputu ve tüm açık combobox'ları kontrol et
            active_input = 0;
            bool any_combobox_was_open = false;
            
            for (int i = 0; i < window_count; i++) {
                for (int c = 0; c < window_list[i].combobox_count; c++) {
                    if (window_list[i].comboboxes[c].is_open) {
                        window_list[i].comboboxes[c].is_open = false;
                        any_combobox_was_open = true;
                    }
                }
            }

            // Eğer açık bir combobox dışarı tıklanarak kapatıldıysa tüm ekranı yenile
            if (any_combobox_was_open) {
                damage_union_rect(0, 0, fb_get_width(), fb_get_height());
            }

            if (active_window != 0) {
                cursor_prepare_redraw();
                
                for (int i = 0; i < window_count; i++) {
                    if (window_list[i].width > 0) {
                        damage_union_rect(window_list[i].x, window_list[i].y, window_list[i].width, window_list[i].height);
                        window_list[i].is_active = false;
                    }
                }
                
                active_window = 0;

                int32_t cur_x, cur_y;
                cursor_get_position(&cur_x, &cur_y);
                damage_union_rect(cur_x, cur_y, CURSOR_WIDTH, CURSOR_HEIGHT);

                desktop_redraw();
                cursor_sync_position();
                cursor_show();
            } else if (any_combobox_was_open) {
                // Eğer etkin bir pencere yok ama sadece açık bir combobox kapatıldıysa yine de ekranı yeniden çiz
                desktop_redraw();
                cursor_sync_position();
                cursor_show();
            }
        }
    }

    // 3. The left button is held and the mouse is moving (dragging or resizing continues)
    else if (left_pressed && prev_left) {
        if (resized_window && resize_direction != RESIZE_NONE) {
            int old_x = resized_window->x;
            int old_y = resized_window->y;
            int old_w = resized_window->width;
            int old_h = resized_window->height;

            int new_x = old_x;
            int new_y = old_y;
            int new_w = old_w;
            int new_h = old_h;

            // Use window-specific minimum limits
            int min_w = resized_window->min_width > 0 ? resized_window->min_width : MIN_WINDOW_WIDTH;
            int min_h = resized_window->min_height > 0 ? resized_window->min_height : MIN_WINDOW_HEIGHT;

            if (resize_direction & RESIZE_LEFT) {
                int max_x = old_x + old_w - min_w;
                new_x = mouse_x;
                if (new_x > max_x) new_x = max_x;
                new_w = old_w + (old_x - new_x);
                if (new_w < min_w) {
                    new_w = min_w;
                    new_x = old_x + old_w - min_w;
                }
            } else if (resize_direction & RESIZE_RIGHT) {
                new_w = mouse_x - old_x;
                if (new_w < min_w) {
                    new_w = min_w;
                }
            }

            if (resize_direction & RESIZE_TOP) {
                int max_y = old_y + old_h - min_h;
                new_y = mouse_y;
                if (new_y > max_y) new_y = max_y;
                new_h = old_h + (old_y - new_y);
            } else if (resize_direction & RESIZE_BOTTOM) {
                new_h = mouse_y - old_y;
                if (new_h < min_h) new_h = min_h;
            }

            if (new_x != old_x || new_y != old_y || new_w != old_w || new_h != old_h) {
                int32_t old_cursor_x, old_cursor_y;
                cursor_get_position(&old_cursor_x, &old_cursor_y);

                cursor_prepare_redraw();

                resized_window->is_active = true;
                active_window = resized_window;

                // --- SOLUTION: Clear both the old area and new area during resizing ---
                damage_union_rect(resize_old_x, resize_old_y, resize_old_w, resize_old_h);
                
                resized_window->x = new_x;
                resized_window->y = new_y;
                resized_window->width = new_w;
                resized_window->height = new_h;

                // --- 1. ANCHOR CALCULATION FOR PANELS ---
                for (int i = 0; i < resized_window->panel_count; i++) {
                    panel_t* p = &resized_window->panels[i];
                    
                    if ((p->anchor & ANCHOR_LEFT) && (p->anchor & ANCHOR_RIGHT)) {
                        p->width = p->init_width + (resized_window->width - p->init_win_w);
                    } else if (p->anchor & ANCHOR_RIGHT) {
                        int right_margin = p->init_win_w - (p->init_x + p->init_width);
                        p->x = resized_window->width - right_margin - p->width;
                    }

                    if ((p->anchor & ANCHOR_TOP) && (p->anchor & ANCHOR_BOTTOM)) {
                        p->height = p->init_height + (resized_window->height - p->init_win_h);
                    } else if (p->anchor & ANCHOR_BOTTOM) {
                        int bottom_margin = p->init_win_h - (p->init_y + p->init_height);
                        p->y = resized_window->height - bottom_margin - p->height;
                    }
                }

                // --- 2. ANCHOR CALCULATION FOR BUTTONS ---
                for (int i = 0; i < resized_window->button_count; i++) {
                    button_t* b = &resized_window->buttons[i];

                    if ((b->anchor & ANCHOR_LEFT) && (b->anchor & ANCHOR_RIGHT)) {
                        b->width = b->init_width + (resized_window->width - b->init_win_w);
                    } else if (b->anchor & ANCHOR_RIGHT) {
                        int right_margin = b->init_win_w - (b->init_x + b->init_width);
                        b->x = resized_window->width - right_margin - b->width;
                    }

                    if ((b->anchor & ANCHOR_TOP) && (b->anchor & ANCHOR_BOTTOM)) {
                        b->height = b->init_height + (resized_window->height - b->init_win_h);
                    } else if (b->anchor & ANCHOR_BOTTOM) {
                        int bottom_margin = b->init_win_h - (b->init_y + b->init_height);
                        b->y = resized_window->height - bottom_margin - b->height;
                    }
                }

                // --- 3. ANCHOR CALCULATION FOR LABELS ---
                for (int i = 0; i < resized_window->label_count; i++) {
                    label_item_t* l = &resized_window->labels[i];

                    if (l->init_win_w == 0) l->init_win_w = resized_window->width;
                    if (l->init_win_h == 0) l->init_win_h = resized_window->height;

                    if (l->anchor & ANCHOR_RIGHT) {
                        int right_margin = l->init_win_w - l->init_x;
                        l->x = resized_window->width - right_margin;
                    }

                    if (l->anchor & ANCHOR_BOTTOM) {
                        int bottom_margin = l->init_win_h - l->init_y;
                        l->y = resized_window->height - bottom_margin;
                    }
                }

                // --- 4. ANCHOR CALCULATION FOR 3D CANVAS ---
                if (resized_window->has_canvas) {
                    // Eğer canvas yatayda veya dikeyde genişletilecekse (örneğin sağa/alta yaslıysa ya da tam kaplıyorsa)
                    // İhtiyacınıza göre burada canvas genişlik/yükseklik veya konum güncellemeleri yapabilirsiniz.
                    // Örneğin pencere boyutuna göre canvas boyutunu oranlamak isterseniz:
                    // resized_window->canvas_w = resized_window->canvas_w + (resized_window->width - old_w);
                    // resized_window->canvas_h = resized_window->canvas_h + (resized_window->height - old_h);
                }

                damage_union_rect(new_x, new_y, new_w, new_h);

                damage_union_rect(old_cursor_x, old_cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT);
                damage_union_rect(mouse_x, mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);

                desktop_redraw();

                cursor_sync_position();
                cursor_show();
            }
        }
        else if (dragged_window && dragged_window->is_dragging) {
            // EĞER pencere tam ekrandaysa ve kullanıcı sürüklemeye başladıysa eski haline döndür
            if (dragged_window->is_maximized) {
                damage_union_rect(0, 0, fb_get_width(), fb_get_height());

                dragged_window->is_maximized = false;
                
                int old_w = dragged_window->prev_width;
                int old_h = dragged_window->prev_height;
                
                dragged_window->width = old_w;
                dragged_window->height = old_h;
                
                dragged_window->x = mouse_x - (old_w / 2);
                dragged_window->y = mouse_y - 12;
                
                dragged_window->drag_offset_x = mouse_x - dragged_window->x;
                dragged_window->drag_offset_y = mouse_y - dragged_window->y;

                // İç elemanların referans boyutlarını eski haline kitle
                dragged_window->init_win_w = dragged_window->width;
                dragged_window->init_win_h = dragged_window->height;

                for (int i = 0; i < dragged_window->panel_count; i++) {
                    dragged_window->panels[i].init_x = dragged_window->panels[i].x;
                    dragged_window->panels[i].init_y = dragged_window->panels[i].y;
                    dragged_window->panels[i].init_width = dragged_window->panels[i].width;
                    dragged_window->panels[i].init_height = dragged_window->panels[i].height;
                    dragged_window->panels[i].init_win_w = dragged_window->width;
                    dragged_window->panels[i].init_win_h = dragged_window->height;
                }
                for (int i = 0; i < dragged_window->button_count; i++) {
                    dragged_window->buttons[i].init_x = dragged_window->buttons[i].x;
                    dragged_window->buttons[i].init_y = dragged_window->buttons[i].y;
                    dragged_window->buttons[i].init_width = dragged_window->buttons[i].width;
                    dragged_window->buttons[i].init_height = dragged_window->buttons[i].height;
                    dragged_window->buttons[i].init_win_w = dragged_window->width;
                    dragged_window->buttons[i].init_win_h = dragged_window->height;
                }

                damage_union_rect(0, 0, fb_get_width(), fb_get_height());
                desktop_redraw();
            }

            int new_x = mouse_x - dragged_window->drag_offset_x;
            int new_y = mouse_y - dragged_window->drag_offset_y;

            int screen_w = fb_get_width();
            int screen_h = fb_get_height();

            if (new_x + dragged_window->width < 50) {
                new_x = 50 - dragged_window->width;
            }
            if (new_x > screen_w - 50) {
                new_x = screen_w - 50;
            }
            if (new_y < 0) {
                new_y = 0;
            }
            
            int taskbar_h = 36;
            if (new_y > screen_h - taskbar_h - WM_TITLEBAR_HEIGHT) {
                new_y = screen_h - taskbar_h - WM_TITLEBAR_HEIGHT;
            }

            if (new_x != dragged_window->x || new_y != dragged_window->y) {
                int old_x = dragged_window->x;
                int old_y = dragged_window->y;
                int32_t old_cursor_x, old_cursor_y;

                cursor_get_position(&old_cursor_x, &old_cursor_y);
                
                cursor_prepare_redraw();

                dragged_window->is_active = true;
                active_window = dragged_window;

                dragged_window->x = new_x;
                dragged_window->y = new_y;

                damage_union_rect(old_x, old_y, dragged_window->width, dragged_window->height);
                damage_union_rect(new_x, new_y, dragged_window->width, dragged_window->height);
                
                damage_union_rect(old_cursor_x, old_cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT);
                damage_union_rect(mouse_x, mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);
                
                desktop_redraw();

                cursor_sync_position();
                cursor_show();
            }
        }
        else {
            int32_t old_cursor_x, old_cursor_y;
            cursor_get_position(&old_cursor_x, &old_cursor_y);

            if (old_cursor_x != mouse_x || old_cursor_y != mouse_y) {
                cursor_prepare_redraw();

                damage_union_rect(old_cursor_x, old_cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT);
                damage_union_rect(mouse_x, mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);

                desktop_redraw();

                cursor_sync_position();
                cursor_show();
            }
        }
    }

    prev_buttons = mouse_buttons;
}

void wm_handle_key_press(char c) {
    if (!active_input) return;

    int len = 0;
    while (active_input->text[len] != '\0') len++;

    // Backspace (Geri silme tuşu)
    if (c == '\b' || c == 127) {
        if (len > 0) {
            active_input->text[len - 1] = '\0';
            damage_union_rect(0, 0, fb_get_width(), fb_get_height());
            desktop_redraw();
        }
        return;
    }

    // Normal yazdırılabilir karakterler
    if (c >= 32 && c < 127 && len < 126) {
        active_input->text[len] = c;
        active_input->text[len + 1] = '\0';
        damage_union_rect(0, 0, fb_get_width(), fb_get_height());
        desktop_redraw();
    }
}

window_t* wm_get_active_window(void) {
    return active_window;
}