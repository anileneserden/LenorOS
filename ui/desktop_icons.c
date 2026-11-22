#include <ui/desktop_icons.h>
#include <ui/grid.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/fs/vfs.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <stddef.h>
#include <kernel/mem/heap.h>
#include <kernel/loader/png.h>
#include <kernel/loader/ico.h>

int snprintf(char *str, size_t size, const char *format, ...);
extern int lex2_load_and_run(const char *path);

extern uint32_t desktop_bg_color;
void damage_union_rect(int x, int y, int w, int h);

#define DOUBLE_CLICK_MAX_TICKS 250000

static int selected_icon_index = -1;
static int last_clicked_index = -1;
static uint32_t last_click_tick = 0;
static uint32_t global_tick_counter = 0;

static int last_hover_index = -1;
static vfs_file_info_t desktop_files[16];
static char desktop_display_names[16][64];
static char desktop_exec_paths[16][256];
static png_image_t* desktop_icon_images[16];
static int desktop_file_count = 0;

static bool has_ext_ci(const char *path, const char *ext) {
    if (!path || !ext) return false;
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return false;

    const char *tail = path + (plen - elen);
    for (size_t i = 0; i < elen; i++) {
        char a = tail[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static void free_icon_images(void) {
    for (int i = 0; i < 16; i++) {
        if (desktop_icon_images[i]) {
            if (desktop_icon_images[i]->pixels) {
                kfree(desktop_icon_images[i]->pixels);
            }
            kfree(desktop_icon_images[i]);
            desktop_icon_images[i] = NULL;
        }
    }
}

static void parse_desktop_entry(const char *full_path,
                                char *out_name, size_t name_max,
                                char *out_icon, size_t icon_max,
                                char *out_exec, size_t exec_max) {
    if (out_name && name_max > 0) out_name[0] = '\0';
    if (out_icon && icon_max > 0) out_icon[0] = '\0';
    if (out_exec && exec_max > 0) out_exec[0] = '\0';
    if (!full_path) return;

    uint32_t size = 0;
    char *buf = (char*)vfs_read_file(full_path, &size);
    if (!buf || size == 0) {
        if (buf) kfree(buf);
        return;
    }

    size_t pos = 0;
    while (pos < size) {
        size_t line_start = pos;
        while (pos < size && buf[pos] != '\n') {
            pos++;
        }
        size_t line_end = pos;
        if (pos < size && buf[pos] == '\n') {
            pos++;
        }

        while (line_end > line_start &&
               (buf[line_end - 1] == '\r' ||
                buf[line_end - 1] == ' '  ||
                buf[line_end - 1] == '\t')) {
            line_end--;
        }

        size_t line_len = line_end - line_start;

        if (out_name && name_max > 0 && line_len > 5 && strncmp(&buf[line_start], "Name=", 5) == 0) {
            size_t val_start = line_start + 5;
            size_t val_len = line_end - val_start;
            if (val_len >= name_max) val_len = name_max - 1;
            memcpy(out_name, &buf[val_start], val_len);
            out_name[val_len] = '\0';
        } else if (out_icon && icon_max > 0 && line_len > 5 && strncmp(&buf[line_start], "Icon=", 5) == 0) {
            size_t val_start = line_start + 5;
            size_t val_len = line_end - val_start;
            if (val_len >= icon_max) val_len = icon_max - 1;
            memcpy(out_icon, &buf[val_start], val_len);
            out_icon[val_len] = '\0';
        } else if (out_exec && exec_max > 0 && line_len > 5 && strncmp(&buf[line_start], "Exec=", 5) == 0) {
            size_t val_start = line_start + 5;
            size_t val_len = line_end - val_start;
            if (val_len >= exec_max) val_len = exec_max - 1;
            memcpy(out_exec, &buf[val_start], val_len);
            out_exec[val_len] = '\0';
        }
    }

    kfree(buf);
}

static png_image_t* load_icon_from_path(const char *icon_path) {
    if (!icon_path || icon_path[0] == '\0') return NULL;

    uint32_t file_size = 0;
    uint8_t *file_data = (uint8_t*)vfs_read_file(icon_path, &file_size);
    if (!file_data || file_size == 0) {
        if (file_data) kfree(file_data);
        serial_write("[DESKTOP ICON] Ikon dosyasi okunamadi: ");
        serial_write(icon_path);
        serial_write("\n");
        return NULL;
    }

    png_image_t *img = NULL;
    if (has_ext_ci(icon_path, ".ico")) {
        img = ico_load_from_memory(file_data, file_size);
    } else if (has_ext_ci(icon_path, ".png")) {
        img = png_load_from_memory(file_data, file_size);
    } else {
        img = ico_load_from_memory(file_data, file_size);
        if (!img) {
            img = png_load_from_memory(file_data, file_size);
        }
    }

    kfree(file_data);

    if (!img || !img->pixels || img->width <= 0 || img->height <= 0) {
        serial_write("[DESKTOP ICON] Ikon decode edilemedi: ");
        serial_write(icon_path);
        serial_write("\n");
        if (img) {
            if (img->pixels) kfree(img->pixels);
            kfree(img);
        }
        return NULL;
    }

    serial_write("[DESKTOP ICON] Ikon yuklendi: ");
    serial_write(icon_path);
    serial_write("\n");
    return img;
}

static void build_desktop_entries(void) {
    free_icon_images();

    for (int i = 0; i < desktop_file_count; i++) {
        desktop_display_names[i][0] = '\0';
        desktop_exec_paths[i][0] = '\0';
        desktop_icon_images[i] = NULL;

        if (!desktop_files[i].is_directory && has_ext_ci(desktop_files[i].name, ".desktop")) {
            char full_path[256];
            snprintf(full_path, sizeof(full_path), "C:/Users/anil/Desktop/%s", desktop_files[i].name);

            char parsed_name[64];
            char parsed_icon[256];
            char parsed_exec[256];
            parse_desktop_entry(full_path,
                                parsed_name, sizeof(parsed_name),
                                parsed_icon, sizeof(parsed_icon),
                                parsed_exec, sizeof(parsed_exec));

            if (parsed_name[0] != '\0') {
                strncpy(desktop_display_names[i], parsed_name, sizeof(desktop_display_names[i]) - 1);
                desktop_display_names[i][sizeof(desktop_display_names[i]) - 1] = '\0';
            }

            if (parsed_icon[0] != '\0') {
                desktop_icon_images[i] = load_icon_from_path(parsed_icon);
            }

            if (parsed_exec[0] != '\0') {
                strncpy(desktop_exec_paths[i], parsed_exec, sizeof(desktop_exec_paths[i]) - 1);
                desktop_exec_paths[i][sizeof(desktop_exec_paths[i]) - 1] = '\0';
            }
        } else if (!desktop_files[i].is_directory && has_ext_ci(desktop_files[i].name, ".lex")) {
            snprintf(desktop_exec_paths[i], sizeof(desktop_exec_paths[i]),
                     "C:/Users/anil/Desktop/%s", desktop_files[i].name);
        }

        if (desktop_display_names[i][0] == '\0') {
            strncpy(desktop_display_names[i], desktop_files[i].name, sizeof(desktop_display_names[i]) - 1);
            desktop_display_names[i][sizeof(desktop_display_names[i]) - 1] = '\0';
        }
    }
}

void desktop_icons_refresh(void) {
    desktop_file_count = vfs_get_directory_files("C:/Users/anil/Desktop", desktop_files, 16);
    if (desktop_file_count < 0) desktop_file_count = 0;
    if (desktop_file_count > 16) desktop_file_count = 16;
    build_desktop_entries();
}

void desktop_icons_init(void) {
    selected_icon_index = -1;
    last_clicked_index = -1;
    last_click_tick = 0;
    global_tick_counter = 0;
    last_hover_index = -1;
    for (int i = 0; i < 16; i++) {
        desktop_icon_images[i] = NULL;
        desktop_exec_paths[i][0] = '\0';
    }
    desktop_icons_refresh();
}

int desktop_icons_get_selected(void) {
    return selected_icon_index;
}

void desktop_icons_tick(void) {
    global_tick_counter++;
}

static void launch_desktop_icon(int idx) {
    if (idx < 0 || idx >= desktop_file_count) return;

    if (desktop_exec_paths[idx][0] != '\0') {
        serial_write("[DESKTOP ICON] Exec calistiriliyor: ");
        serial_write(desktop_exec_paths[idx]);
        serial_write("\n");

        if (!lex2_load_and_run(desktop_exec_paths[idx])) {
            serial_write("[DESKTOP ICON ERROR] Uygulama baslatilamadi: ");
            serial_write(desktop_exec_paths[idx]);
            serial_write("\n");
        }
    } else if (!desktop_files[idx].is_directory && has_ext_ci(desktop_files[idx].name, ".desktop")) {
        serial_write("[DESKTOP ICON WARN] Exec= alani bos: ");
        serial_write(desktop_files[idx].name);
        serial_write("\n");
    }
}

bool desktop_icons_handle_click(int32_t mx, int32_t my) {
    int cell_w = grid_get_cell_width();
    int cell_h = grid_get_cell_height();

    int hit_index = -1;
    for (int i = 0; i < desktop_file_count; i++) {
        int gx = 0, gy = 0;
        grid_get_position(i, &gx, &gy);

        if (mx >= gx && mx < (gx + cell_w) &&
            my >= gy && my < (gy + cell_h)) {
            hit_index = i;
            break;
        }
    }

    if (hit_index == -1) {
        if (selected_icon_index != -1) {
            int prev_gx = 0, prev_gy = 0;
            grid_get_position(selected_icon_index, &prev_gx, &prev_gy);
            damage_union_rect(prev_gx, prev_gy, cell_w, cell_h);
            selected_icon_index = -1;
        }
        last_clicked_index = -1;
        return false;
    }

    uint32_t tick_diff = global_tick_counter - last_click_tick;
    serial_write("[DBLCLK] hit=");
    serial_write_dec((uint32_t)hit_index);
    serial_write(" last=");
    serial_write_dec((uint32_t)last_clicked_index);
    serial_write(" tick=");
    serial_write_dec(global_tick_counter);
    serial_write(" last_tick=");
    serial_write_dec(last_click_tick);
    serial_write(" diff=");
    serial_write_dec(tick_diff);
    serial_write("\n");

    bool is_double_click = (last_clicked_index == hit_index) &&
                           (tick_diff <= DOUBLE_CLICK_MAX_TICKS);

    if (selected_icon_index != hit_index) {
        if (selected_icon_index != -1) {
            int prev_gx = 0, prev_gy = 0;
            grid_get_position(selected_icon_index, &prev_gx, &prev_gy);
            damage_union_rect(prev_gx, prev_gy, cell_w, cell_h);
        }
        selected_icon_index = hit_index;
    }

    int cur_gx = 0, cur_gy = 0;
    grid_get_position(hit_index, &cur_gx, &cur_gy);
    damage_union_rect(cur_gx, cur_gy, cell_w, cell_h);

    if (is_double_click) {
        last_clicked_index = -1;
        last_click_tick = 0;
        launch_desktop_icon(hit_index);
    } else {
        last_clicked_index = hit_index;
        last_click_tick = global_tick_counter;
    }

    return true;
}

// Metin genişliği kutuyu aşıyorsa metnin sonuna '...' koyarak kısaltır
static void truncate_text(const char *src, char *dst, size_t max_dst, int max_chars) {
    int len = strlen(src);
    if (len <= max_chars) {
        strncpy(dst, src, max_dst);
        dst[max_dst - 1] = '\0';
    } else {
        if (max_chars > 3) {
            strncpy(dst, src, max_chars - 3);
            dst[max_chars - 3] = '\0';
            strcat(dst, "...");
        } else {
            strncpy(dst, src, max_chars);
            dst[max_chars] = '\0';
        }
    }
}

static void draw_scaled_icon(const png_image_t *img, int dst_x, int dst_y, int dst_w, int dst_h) {
    if (!img || !img->pixels || img->width <= 0 || img->height <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (dy * img->height) / dst_h;
        if (sy >= img->height) sy = img->height - 1;

        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (dx * img->width) / dst_w;
            if (sx >= img->width) sx = img->width - 1;

            uint32_t px = img->pixels[sy * img->width + sx];
            uint8_t alpha = (px >> 24) & 0xFF;
            if (alpha > 0) {
                gfx_fill_rect(dst_x + dx, dst_y + dy, 1, 1, px);
            }
        }
    }
}

void desktop_icons_draw(int32_t mx, int32_t my, bool click_started) {
    (void)click_started;

    int cell_w = grid_get_cell_width();  // 100 px
    int cell_h = grid_get_cell_height(); // 100 px

    int current_hover_index = -1;

    // 1. Mouse'un hangi ikon hücresinde olduğunu tespit et
    for (int i = 0; i < desktop_file_count; i++) {
        int gx = 0, gy = 0;
        grid_get_position(i, &gx, &gy);

        if (mx >= gx && mx < (gx + cell_w) &&
            my >= gy && my < (gy + cell_h)) {
            current_hover_index = i;
            break;
        }
    }

    // 2. Hover değişimi takibi (Damage yönetimi)
    if (current_hover_index != last_hover_index) {
        if (last_hover_index != -1 && last_hover_index < desktop_file_count) {
            int prev_gx = 0, prev_gy = 0;
            grid_get_position(last_hover_index, &prev_gx, &prev_gy);
            damage_union_rect(prev_gx, prev_gy, cell_w, cell_h);
        }

        if (current_hover_index != -1) {
            int new_gx = 0, new_gy = 0;
            grid_get_position(current_hover_index, &new_gx, &new_gy);
            damage_union_rect(new_gx, new_gy, cell_w, cell_h);
        }

        last_hover_index = current_hover_index;
    }

    // 3. İkonları Çiz
    for (int i = 0; i < desktop_file_count; i++) {
        int gx = 0, gy = 0;
        grid_get_position(i, &gx, &gy);

        // Grid hücresine göre sınırlandırılmış hover/seçim kutusu (Örn: 88x88px)
        int box_w = 88;
        int box_h = 88;
        
        int box_x = gx + (cell_w - box_w) / 2;
        int box_y = gy + (cell_h - box_h) / 2;

        bool is_hovered = (i == current_hover_index);
        bool is_selected = (i == selected_icon_index);
        uint32_t bg_color = is_selected ? 0xFF2B5B84 : (is_hovered ? 0xFF3A4D6B : desktop_bg_color);

        // Hover, seçim veya zemin temizliği için kutuyu doldur
        gfx_fill_rect(box_x, box_y, box_w, box_h, bg_color);

        // --- İKONU KUTU İÇİNDE ORTALAMA ---
        int icon_w = 36;
        int icon_h = 36;
        int icon_x = box_x + (box_w - icon_w) / 2;
        int icon_y = box_y + 8;

        bool is_dir = desktop_files[i].is_directory;

        if (desktop_icon_images[i]) {
            draw_scaled_icon(desktop_icon_images[i], icon_x, icon_y, icon_w, icon_h);
        } else if (is_dir) {
            // --- SARI KLASÖR SİMGESİ ---
            gfx_fill_rect(icon_x + 4, icon_y + 6, 12, 5, 0xFFD4AC0D);
            gfx_fill_rect(icon_x + 2, icon_y + 10, icon_w - 4, icon_h - 12, 0xFFF1C40F);
            gfx_fill_rect(icon_x + 5, icon_y + 15, icon_w - 10, 2, 0xD4AC0DFF);
        } else {
            // --- DOSYA / BELGE SİMGESİ ---
            gfx_fill_rect(icon_x, icon_y, icon_w, icon_h, 0xFFCCCCCC);
            gfx_fill_rect(icon_x + 3, icon_y + 3, icon_w - 6, icon_h - 6, 0xFFFFFFFF);
            gfx_fill_rect(icon_x + 7, icon_y + 9, 14, 2, 0xFF555555);
            gfx_fill_rect(icon_x + 7, icon_y + 15, 18, 2, 0xFF555555);
        }

        // --- METNİ KISALTMA VE ORTALAMA ---
        char display_name[32];
        int char_width = 8;
        int max_chars = (box_w - 8) / char_width; // 10 karakter

        const char *raw_label = (desktop_display_names[i][0] != '\0')
                                ? desktop_display_names[i]
                                : desktop_files[i].name;
        truncate_text(raw_label, display_name, sizeof(display_name), max_chars);

        int text_w = strlen(display_name) * char_width;
        int text_x = box_x + (box_w - text_w) / 2;
        int text_y = icon_y + icon_h + 8;

        gfx_draw_text_utf8(text_x, text_y, 0xFFFFFFFF, display_name);
    }
}