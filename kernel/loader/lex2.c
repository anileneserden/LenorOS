#include <kernel/loader/lex2.h>
#include <kernel/fs/vfs.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <ui/wm.h>
#include <kernel/drivers/video/gfx.h>
#include <ui/desktop.h>
#include <kernel/power.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <kernel/loader/png.h>
#include <kernel/loader/ico.h>

#define WM_TITLEBAR_HEIGHT 24

#define ANCHOR_LEFT   (1 << 0)
#define ANCHOR_RIGHT  (1 << 1)
#define ANCHOR_TOP    (1 << 2)
#define ANCHOR_BOTTOM (1 << 3)

// Aktif LEX dosyasını ve kaynak tablosunu bellekte tutmak için global değişkenler
static uint8_t* current_lex_file_buffer = 0;
static uint32_t current_lex_file_size = 0;

static void lex_print(const char* str) {
    serial_write(str);
}

static int lex_window_create(const char* title, int width, int height, bool resizable) {
    (void)resizable;
    window_t* window = wm_create_window(width, height, title);
    return window != 0;
}

static void* lex_window_create_with_tabs(int width, int height, const char* title, int initial_tab_count) {
    return wm_create_window_with_tabs(width, height, title, initial_tab_count);
}

static void lex_background_color(uint32_t color) {
    window_t* win = wm_get_active_window();
    if (!win) return;
    
    win->bg_color = color;
    damage_union_rect(win->x, win->y, win->width, win->height);
    wm_draw_window(win);
    desktop_redraw();
}

static void lex_exit(void) {
    serial_write("LEXV2: Application called the exit API; returning to the kernel.\n");
}

static int lex_panel_create(int x, int y, int w, int h, uint32_t color, uint32_t hover_color, void (*on_click)(void), void (*on_hover)(void), uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->panel_count >= MAX_PANELS) return -1;

    panel_t* p = &win->panels[win->panel_count++];
    p->x = x; p->y = y; p->width = w; p->height = h; 
    p->color = color;
    p->hover_color = hover_color;     
    p->on_click = on_click;           
    p->on_hover = on_hover;           
    p->anchor = anchor;
    p->init_x = x; p->init_y = y; p->init_width = w; p->init_height = h;
    p->init_win_w = win->width; p->init_win_h = win->height;

    return 1;
}

static int lex_label_create(int x, int y, uint32_t color, const char* text, uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->label_count >= MAX_LABELS) return -1;

    label_item_t* l = &win->labels[win->label_count++];
    l->x = x; l->y = y; l->color = color; l->anchor = anchor;
    l->init_x = x; l->init_y = y;
    l->init_win_w = win->width; l->init_win_h = win->height;
    
    int i = 0;
    while (text[i] != '\0' && i < 63) { l->text[i] = text[i]; i++; }
    l->text[i] = '\0';

    return 1;
}

static int lex_button_create(int x, int y, int w, int h, uint32_t bg_color, uint32_t text_color, const char* text, void (*on_click)(void), uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->button_count >= MAX_BUTTONS) return -1;

    button_t* b = &win->buttons[win->button_count++];
    b->x = x; b->y = y; b->width = w; b->height = h;
    b->bg_color = bg_color; b->text_color = text_color; b->on_click = on_click;
    b->anchor = anchor;
    b->init_x = x; b->init_y = y; b->init_width = w; b->init_height = h;
    b->init_win_w = win->width; b->init_win_h = win->height;

    int i = 0;
    while (text[i] != '\0' && i < 31) { b->text[i] = text[i]; i++; }
    b->text[i] = '\0';

    wm_draw_window(win);
    return 1;
}

static uint8_t* lex_find_resource_in_memory(const char* target_filename, uint32_t* out_size) {
    if (!current_lex_file_buffer || current_lex_file_size < sizeof(lex2_header_t)) return 0;

    serial_write("[LEX DEBUG] Aranan kaynak: ");
    serial_write(target_filename);
    serial_write("\n");

    lex2_header_t* header = (lex2_header_t*)current_lex_file_buffer;
    uint32_t current_offset = sizeof(lex2_header_t);

    for (uint32_t i = 0; i < header->section_count; i++) {
        if (current_offset + sizeof(lex2_section_header_t) > current_lex_file_size) break;
        lex2_section_header_t* sec = (lex2_section_header_t*)(current_lex_file_buffer + current_offset);
        current_offset += sizeof(lex2_section_header_t);

        if (sec->type == LEX2_SECTION_RESOURCES) {
            uint8_t* section_ptr = current_lex_file_buffer + sec->offset;
            uint32_t section_left = sec->size;

            while (section_left > sizeof(uint32_t)) {
                char* fname = (char*)section_ptr;
                size_t fname_len = strlen(fname);
                if (fname_len == 0) break;

                serial_write("[LEX DEBUG] Paketteki kaynak: ");
                serial_write(fname);
                serial_write("\n");

                uint8_t* cursor = section_ptr + fname_len + 1;
                uint32_t consumed = fname_len + 1;

                if (consumed + sizeof(uint32_t) > section_left) break;

                uint32_t data_size = *(uint32_t*)cursor;
                cursor += sizeof(uint32_t);
                consumed += sizeof(uint32_t);

                if (consumed + data_size > section_left) break;

                uint8_t* data_ptr = cursor;
                cursor += data_size;
                consumed += data_size;

                if (consumed + 6 <= section_left) {
                    if (cursor[0] == 'I' && cursor[1] == 'M' && cursor[2] == 'G' &&
                        cursor[3] == 'E' && cursor[4] == 'N' && cursor[5] == 'D') {
                        cursor += 6;
                        consumed += 6;
                    }
                }

                section_left -= consumed;
                section_ptr = cursor;

                if (strcmp(fname, target_filename) == 0 || strstr(fname, target_filename) != NULL) {
                    serial_write("[LEX DEBUG] Kaynak basariyla eslesti!\n");
                    *out_size = data_size;
                    return data_ptr;
                }
            }
        }
    }
    serial_write("[LEX DEBUG] Kaynak bulunamadi!\n");
    return 0;
}

static int lex_picturebox_create(int x, int y, int w, int h, const char* img_path, uint8_t anchor, int size_mode) {
    (void)size_mode;
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->picturebox_count >= MAX_PICTUREBOXES) return -1;

    picturebox_t* p = &win->pictureboxes[win->picturebox_count++];
    p->x = x; p->y = y; p->width = w; p->height = h;
    p->anchor = anchor;
    p->init_x = x; p->init_y = y; p->init_width = w; p->init_height = h;
    p->init_win_w = win->width; p->init_win_h = win->height;
    p->pixels = 0;
    p->img_width = 0;
    p->img_height = 0;
    
    int i = 0;
    while (img_path[i] != '\0' && i < 63) { p->image_path[i] = img_path[i]; i++; }
    p->image_path[i] = '\0';

    uint8_t* img_raw_data = 0;
    uint32_t img_data_size = 0;

    if (img_path[0] == 'C' && img_path[1] == ':' && (img_path[2] == '/' || img_path[2] == '\\')) {
        img_raw_data = (uint8_t*)vfs_read_file(img_path, &img_data_size);
        if (!img_raw_data || img_data_size == 0) return -1;
    } else {
        img_raw_data = lex_find_resource_in_memory(img_path, &img_data_size);
        if (!img_raw_data || img_data_size == 0) return -1;
    }

    png_image_t* img = png_load_from_memory(img_raw_data, img_data_size);
    if (img_path[0] == 'C' && img_raw_data) {
        kfree(img_raw_data);
    }

    if (!img) return -1;

    p->img_width = img->width;
    p->img_height = img->height;
    
    size_t pixel_buf_size = img->width * img->height * sizeof(uint32_t);
    p->pixels = (uint32_t*)kmalloc(pixel_buf_size);
    if (p->pixels && img->pixels) {
        memcpy(p->pixels, img->pixels, pixel_buf_size);
    }

    png_free_image(img);
    wm_draw_window(win);
    return 1;
}

static int lex_input_create(int x, int y, int w, int h, 
                            const char* text, const char* placeholdertext, 
                            uint32_t backcolor, uint32_t color, 
                            uint32_t placeholder_color, uint32_t border_color, 
                            int border_thickness, void (*on_key)(char c), uint8_t anchor) {
    (void)on_key;
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->input_count >= MAX_INPUTS) return -1;

    text_input_t* inp = &win->inputs[win->input_count++];
    inp->x = x; inp->y = y; inp->width = w; inp->height = h;
    inp->bg_color = backcolor; 
    inp->text_color = color;
    inp->placeholder_color = placeholder_color;
    inp->border_color = border_color;
    inp->border_thickness = border_thickness;
    inp->anchor = anchor;
    inp->init_x = x; inp->init_y = y; 
    inp->init_width = w; inp->init_height = h;
    inp->init_win_w = win->width; inp->init_win_h = win->height;

    int i = 0;
    if (text) {
        while (text[i] != '\0' && i < 127) { inp->text[i] = text[i]; i++; }
    }
    inp->text[i] = '\0';

    int j = 0;
    if (placeholdertext) {
        while (placeholdertext[j] != '\0' && j < 127) { inp->placeholder[j] = placeholdertext[j]; j++; }
    }
    inp->placeholder[j] = '\0';

    wm_draw_window(win);
    return 1;
}

static int lex_input_get_text(int input_id, char* out_buf, int max_len) {
    window_t* win = wm_get_active_window();
    if (!win || input_id < 0 || input_id >= win->input_count) return -1;

    text_input_t* inp = &win->inputs[input_id];
    int i = 0;
    while (inp->text[i] != '\0' && i < max_len - 1) {
        out_buf[i] = inp->text[i];
        i++;
    }
    out_buf[i] = '\0';
    return i;
}

static int lex_combobox_create(int x, int y, int w, int h, const char** items, int item_count, int default_index, uint32_t bg_color, uint32_t text_color, uint32_t border_color, uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;
    if (win->combobox_count >= MAX_COMBOBOXES) return -1;

    combobox_t* cb = &win->comboboxes[win->combobox_count++];
    cb->x = x; cb->y = y; cb->width = w; cb->height = h;
    cb->items = items;
    cb->item_count = item_count;
    cb->selected_index = (default_index >= 0 && default_index < item_count) ? default_index : 0;
    cb->is_open = false;
    cb->bg_color = bg_color;
    cb->text_color = text_color;
    cb->border_color = border_color;
    cb->anchor = anchor;
    cb->init_x = x; cb->init_y = y; 
    cb->init_width = w; cb->init_height = h;
    cb->init_win_w = win->width; cb->init_win_h = win->height;

    wm_draw_window(win);
    return 1;
}

static int lex_get_directory_files(const char* full_path, vfs_file_info_t* out_list, int max_count) {
    return vfs_get_directory_files(full_path, out_list, max_count);
}

static void* lex_read_file(const char* full_path, uint32_t* out_size) {
    return vfs_read_file(full_path, out_size);
}

static int lex_strcmp(const char* s1, const char* s2) {
    return strcmp(s1, s2);
}

static size_t lex_strlen(const char* str) {
    return strlen(str);
}

static void lex_yield(void) {
    wm_process_input();
    __asm__ volatile("pause");
}

static uint32_t* lex_canvas_create(int x, int y, int w, int h, uint8_t anchor, int* out_canvas_id) {
    window_t* win = wm_get_active_window();
    if (!win || win->has_canvas) return 0;

    win->has_canvas = true;
    win->canvas_x = x;
    win->canvas_y = y;
    win->canvas_w = w;
    win->canvas_h = h;
    win->canvas_buffer = (uint32_t*)kmalloc(w * h * sizeof(uint32_t));
    win->canvas_anchor = anchor;

    if (out_canvas_id) *out_canvas_id = 1;
    return win->canvas_buffer;
}

static void lex_canvas_update_buffer(int canvas_id) {
    (void)canvas_id; 
    window_t* win = wm_get_active_window();
    if (!win || !win->has_canvas) return;

    damage_union_rect(win->x, win->y, win->width, win->height);
    wm_draw_window(win);
    desktop_redraw();
}

static void lex_reboot_system(void) { system_reboot(); }
static void lex_shutdown_system(void) { system_shutdown(); }
static bool lex_is_key_pressed(int key_code) { return keyboard_is_key_pressed(key_code); }

// Aktif pencereye bir browser bileşeni ekler
int lex_browser_create(int x, int y, int w, int h, uint8_t anchor) {
    window_t* win = wm_get_active_window();
    if (!win) return -1;

    // Eğer pencere yapında browser için özel bir alan tutuyorsan (örneğin has_browser, browser_x vb.)
    // burada set edebilirsin. Ya da özel bir panel/canvas gibi yönetebilirsin.
    win->has_canvas = true; // Şimdilik entegrasyon için canvas mantığını veya özel bayrağı kullanabiliriz
    win->canvas_x = x;
    win->canvas_y = y;
    win->canvas_w = w;
    win->canvas_h = h;
    win->canvas_anchor = anchor;

    // Eğer ayrı bir browser struct yapısı tutuyorsan buraya ekleyebilirsin.
    serial_write("LEX API: browser_create çağrıldı\n");
    return 0; // Başarılı ID
}

static bool starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return false;
    }
    return true;
}

// Aktif penceredeki browser bileşenine HTML içeriği yükler ve parse eder
void lex_browser_load_html(const char* html) {
    window_t* win = wm_get_active_window();
    if (!win || !html) return;

    serial_write("LEX API: browser_load_html çağrıldı. HTML parse ediliyor...\n");

    // Yeni sayfaya geçilirken önceki sayfanın linklerini ve etiketlerini sıfırla
    win->link_count = 0;
    win->label_count = 0;

    int current_y = 60;
    const char* ptr = html;
    bool inside_body = false;
    int safety_counter = 0;
    int col_index = 0;
    uint32_t current_text_color = 0xFFCCCCCC; // Varsayılan metin rengi

    while (*ptr != '\0' && current_y < 500 && safety_counter < 10000) {
        safety_counter++;
        
        // <body> kontrolü
        if (starts_with(ptr, "<body>") || starts_with(ptr, "<BODY>")) {
            inside_body = true;
            ptr += 6;
            continue;
        }
        else if (starts_with(ptr, "</body>") || starts_with(ptr, "</BODY>")) {
            inside_body = false;
            ptr += 7;
            continue;
        }

        if (inside_body) {
            // <br> veya <br/>
            if (starts_with(ptr, "<br>") || starts_with(ptr, "<br/>") || starts_with(ptr, "<BR>")) {
                ptr += (starts_with(ptr, "<br/>") ? 5 : 4);
                current_y += 20;
                continue;
            }
            // <table>
            else if (starts_with(ptr, "<table>") || starts_with(ptr, "<TABLE>")) {
                ptr += 7;
                current_y += 10;
                continue;
            }
            // </table>
            else if (starts_with(ptr, "</table>") || starts_with(ptr, "</TABLE>")) {
                ptr += 8;
                current_y += 15;
                continue;
            }
            // <tr>
            else if (starts_with(ptr, "<tr>") || starts_with(ptr, "<TR>")) {
                ptr += 4;
                current_y += 25;
                col_index = 0;
                continue;
            }
            // </tr>
            else if (starts_with(ptr, "</tr>") || starts_with(ptr, "</TR>")) {
                ptr += 5;
                continue;
            }
            // <td>
            else if (starts_with(ptr, "<td>") || starts_with(ptr, "<TD>")) {
                ptr += 4;
                char cell_buf[64];
                int i = 0;
                while (*ptr && !starts_with(ptr, "</td>") && !starts_with(ptr, "</TD>") && i < 63) {
                    cell_buf[i++] = *ptr++;
                }
                cell_buf[i] = '\0';
                
                int cell_x = 30 + (col_index * 140);
                
                if (win->label_count < MAX_LABELS) {
                    label_item_t* l = &win->labels[win->label_count++];
                    l->x = cell_x; l->y = current_y; l->color = current_text_color; l->anchor = ANCHOR_LEFT;
                    l->init_x = cell_x; l->init_y = current_y;
                    l->init_win_w = win->width; l->init_win_h = win->height;
                    strcpy(l->text, cell_buf);
                }
                col_index++;
                
                if (starts_with(ptr, "</td>") || starts_with(ptr, "</TD>")) ptr += 5;
                continue;
            }
            // Bağlantı Etiketi: <a href="...">Metin</a>
            else if (starts_with(ptr, "<a href=\"") || starts_with(ptr, "<A HREF=\"")) {
                ptr += 9;
                char href_buf[128];
                int i = 0;
                while (*ptr && *ptr != '"' && i < 127) {
                    href_buf[i++] = *ptr++;
                }
                href_buf[i] = '\0';
                
                if (*ptr == '"') ptr++; 
                if (*ptr == '>') ptr++; 

                char text_buf[64];
                i = 0;
                while (*ptr && !starts_with(ptr, "</a>") && !starts_with(ptr, "</A>") && i < 63) {
                    text_buf[i++] = *ptr++;
                }
                text_buf[i] = '\0';

                int link_x = 30;
                
                // Mavi renkli link metnini etiket olarak ekle
                if (win->label_count < MAX_LABELS) {
                    label_item_t* l = &win->labels[win->label_count++];
                    l->x = link_x; l->y = current_y; l->color = 0xFF007ACC; l->anchor = ANCHOR_LEFT;
                    l->init_x = link_x; l->init_y = current_y;
                    l->init_win_w = win->width; l->init_win_h = win->height;
                    strcpy(l->text, text_buf);
                }

                // Pencere yöneticisinin el imleci (hand cursor) algılaması için linki kaydet
                if (win->link_count < MAX_LINKS) {
                    browser_link_t* lnk = &win->links[win->link_count++];
                    lnk->x = link_x;
                    lnk->y = current_y;
                    lnk->width = i * 8;
                    lnk->height = 18;
                    strcpy(lnk->href, href_buf);
                    strcpy(lnk->text, text_buf);
                }

                current_y += 25;
                if (starts_with(ptr, "</a>") || starts_with(ptr, "</A>")) ptr += 4;
                continue;
            }
            // <h1>
            else if (starts_with(ptr, "<h1>") || starts_with(ptr, "<H1>")) {
                ptr += 4;
                char buf[64];
                int i = 0;
                while (*ptr && !starts_with(ptr, "</h1>") && !starts_with(ptr, "</H1>") && i < 63) {
                    buf[i++] = *ptr++;
                }
                buf[i] = '\0';
                
                if (win->label_count < MAX_LABELS) {
                    label_item_t* l = &win->labels[win->label_count++];
                    l->x = 30; l->y = current_y; l->color = current_text_color; l->anchor = ANCHOR_LEFT;
                    l->init_x = 30; l->init_y = current_y;
                    l->init_win_w = win->width; l->init_win_h = win->height;
                    strcpy(l->text, buf);
                }
                current_y += 35;
                if (starts_with(ptr, "</h1>") || starts_with(ptr, "</H1>")) ptr += 5;
                continue;
            }
            // <p>
            else if (starts_with(ptr, "<p>") || starts_with(ptr, "<P>")) {
                ptr += 3;
                char buf[128];
                int i = 0;
                while (*ptr && !starts_with(ptr, "</p>") && !starts_with(ptr, "</P>") && i < 127) {
                    buf[i++] = *ptr++;
                }
                buf[i] = '\0';
                
                if (win->label_count < MAX_LABELS) {
                    label_item_t* l = &win->labels[win->label_count++];
                    l->x = 30; l->y = current_y; l->color = current_text_color; l->anchor = ANCHOR_LEFT;
                    l->init_x = 30; l->init_y = current_y;
                    l->init_win_w = win->width; l->init_win_h = win->height;
                    strcpy(l->text, buf);
                }
                current_y += 25;
                if (starts_with(ptr, "</p>") || starts_with(ptr, "</P>")) ptr += 4;
                continue;
            }
            // Ham Metinler (Raw Text)
            else if (*ptr != '<' && *ptr != '\n' && *ptr != '\r' && *ptr != ' ' && *ptr != '\t') {
                char text_buf[128];
                int i = 0;
                while (*ptr && *ptr != '<' && *ptr != '\n' && *ptr != '\r' && i < 127) {
                    text_buf[i++] = *ptr++;
                }
                text_buf[i] = '\0';
                
                if (i > 0) {
                    if (win->label_count < MAX_LABELS) {
                        label_item_t* l = &win->labels[win->label_count++];
                        l->x = 30; l->y = current_y; l->color = current_text_color; l->anchor = ANCHOR_LEFT;
                        l->init_x = 30; l->init_y = current_y;
                        l->init_win_w = win->width; l->init_win_h = win->height;
                        strcpy(l->text, text_buf);
                    }
                    current_y += 22;
                }
                continue;
            }
        }
        ptr++;
    }

    // Ekranı tazele
    damage_union_rect(win->x, win->y, win->width, win->height);
    wm_draw_window(win);
    desktop_redraw();
}

static void lex_set_tab_icon_from_memory(const void* ico_data, uint32_t ico_size) {
    window_t* win = wm_get_active_window();
    if (!win || !win->has_tabs || win->tab_count <= 0) return;

    // Aktif sekmeyi seçiyoruz
    int active_idx = win->active_tab_index;
    if (active_idx < 0 || active_idx >= win->tab_count) active_idx = 0;

    browser_tab_t* tab = &win->tabs[active_idx];

    // Ham ICO verisini bellekten decode et
    png_image_t* ico_img = ico_load_from_memory((const uint8_t*)ico_data, ico_size);
    if (!ico_img || !ico_img->pixels) {
        serial_write("[LEX ERROR] Sekme ikonu (ICO) decode edilemedi!\n");
        return;
    }

    tab->icon_w = ico_img->width;
    tab->icon_h = ico_img->height;

    size_t pix_size = tab->icon_w * tab->icon_h * sizeof(uint32_t);
    
    // Eğer daha önceden atanmış bir ikon belleği varsa temizleyebiliriz
    if (tab->icon_pixels) {
        kfree(tab->icon_pixels);
    }

    tab->icon_pixels = (uint32_t*)kmalloc(pix_size);
    if (tab->icon_pixels && ico_img->pixels) {
        memcpy(tab->icon_pixels, ico_img->pixels, pix_size);
        serial_write("[LEX] Sekme ikonu basariyla bellege islendi!\n");
    }

    png_free_image(ico_img);

    // Pencereyi ve ekranı tazele ki yeni ikon hemen görünsün
    damage_union_rect(win->x, win->y, win->width, win->height);
    wm_draw_window(win);
    desktop_redraw();
}

static void lex_install_api(void) {
    volatile lex_api_t* api = (volatile lex_api_t*)LEX_API_ADDRESS;
    api->window_create = lex_window_create;
    api->window_create_with_tabs = lex_window_create_with_tabs;
    api->print = lex_print;
    api->exit = lex_exit;
    api->label_create = lex_label_create;
    api->panel_create = lex_panel_create;
    api->button_create = lex_button_create;
    api->picturebox_create = lex_picturebox_create;
    api->input_create = lex_input_create;
    api->input_get_text = lex_input_get_text;
    api->combobox_create = lex_combobox_create;
    api->get_directory_files = lex_get_directory_files;
    api->read_file = lex_read_file;
    api->strcmp = lex_strcmp;
    api->strlen = lex_strlen;
    api->yield = lex_yield;
    api->canvas_create = lex_canvas_create;
    api->canvas_update_buffer = lex_canvas_update_buffer;
    api->background_color = lex_background_color;
    api->reboot_system = lex_reboot_system;
    api->shutdown_system = lex_shutdown_system;
    api->is_key_pressed = lex_is_key_pressed;
    api->browser_create = lex_browser_create;
    api->browser_load_html = lex_browser_load_html;
    api->set_tab_icon_from_memory = lex_set_tab_icon_from_memory;
}

// LEXv2 Yükleyici Fonksiyonu
bool lex2_load_and_run(const char* path) {
    uint32_t file_size = 0;
    uint8_t* file = (uint8_t*)vfs_read_file(path, &file_size);

    if (!file || file_size < sizeof(lex2_header_t)) {
        serial_write("LEXV2: File could not be read or is too small.\n");
        return false;
    }

    lex2_header_t* header = (lex2_header_t*)file;
    if (header->magic != LEX2_MAGIC || header->version != LEX2_VERSION) {
        serial_write("LEXV2: Invalid magic or version.\n");
        kfree(file);
        return false;
    }

    // Eski LEX buffer'ı varsa temizle, yenisini sakla
    if (current_lex_file_buffer) {
        kfree(current_lex_file_buffer);
    }
    current_lex_file_buffer = file;
    current_lex_file_size = file_size;

    uint32_t current_offset = sizeof(lex2_header_t);
    bool text_loaded = false;

    for (uint32_t i = 0; i < header->section_count; i++) {
        if (current_offset + sizeof(lex2_section_header_t) > file_size) {
            serial_write("LEXV2: Section header out of bounds.\n");
            return false;
        }

        lex2_section_header_t* sec = (lex2_section_header_t*)(file + current_offset);
        current_offset += sizeof(lex2_section_header_t);

        if (sec->type == LEX2_SECTION_TEXT) {
            if (sec->offset + sec->size > file_size) {
                serial_write("LEXV2: Text section out of bounds.\n");
                return false;
            }

            if (LEX2_LOAD_ADDRESS + sec->size >= LEX_API_ADDRESS) {
                serial_write("LEXV2: Payload overlaps the API address.\n");
                return false;
            }

            uint8_t* payload = (uint8_t*)LEX2_LOAD_ADDRESS;
            memcpy(payload, file + sec->offset, sec->size);
            text_loaded = true;
            
            serial_write("LEXV2: Text section loaded to 0x400000\n");
            break;
        }
    }

    if (!text_loaded) {
        serial_write("LEXV2: No TEXT section found in package!\n");
        return false;
    }

    lex_install_api();

    serial_write("LEXV2: Calling application entry point (0x400000).\n");
    lex_entry_t entry = (lex_entry_t)LEX2_LOAD_ADDRESS;
    (void)entry();

    window_t* win = wm_get_active_window();
    if (win) {
        damage_union_rect(win->x, win->y, win->width, win->height);
        wm_draw_window(win);
        desktop_redraw();
    }

    serial_write("LEXV2: Application returned.\n");
    return true;
}