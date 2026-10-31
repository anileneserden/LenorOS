#include <ui/renderer3d.h>
#include <ui/camera.h>
#include <ui/cursor.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/string.h>
#include <stdbool.h>

// Harici snprintf ve ortak girdi değişkenlerinin bildirimleri
extern int snprintf(char *str, size_t size, const char *format, ...);
extern int32_t mouse_x;
extern int32_t mouse_y;
extern uint8_t mouse_buttons;
extern uint8_t keyboard_get_last_scancode(void);

// Mutlak değer yardımı
static inline int abs_val(int val) {
    return val < 0 ? -val : val;
}

// --- HIZLI TAMSAYI TRİGONOMETRİ TABLOSU (0 - 359 Derece, 1024 Ölçekli) ---
static const int sine_table[360] = {
    0, 17, 35, 53, 71, 89, 106, 124, 142, 160, 177, 195, 213, 230, 248, 265, 282, 300, 317, 334, 
    352, 368, 385, 402, 419, 436, 452, 469, 485, 501, 517, 533, 549, 564, 580, 595, 610, 625, 640, 655, 
    669, 684, 698, 711, 725, 738, 751, 764, 777, 790, 802, 814, 825, 837, 848, 859, 870, 880, 891, 901, 
    910, 920, 929, 937, 946, 954, 962, 969, 976, 983, 989, 995, 1000, 1005, 1010, 1014, 1018, 1021, 1023, 1024, 
    1024, 1024, 1023, 1021, 1018, 1014, 1010, 1005, 1000, 995, 989, 983, 976, 969, 962, 954, 946, 937, 929, 920, 
    910, 901, 891, 880, 870, 859, 848, 837, 825, 814, 802, 790, 777, 764, 751, 738, 725, 711, 698, 684, 
    669, 655, 640, 625, 610, 595, 580, 564, 549, 533, 517, 501, 485, 469, 452, 436, 419, 402, 385, 368, 
    352, 334, 317, 300, 282, 265, 248, 230, 213, 195, 177, 160, 142, 124, 106, 89, 71, 53, 35, 17, 
    0, -17, -35, -53, -71, -89, -106, -124, -142, -160, -177, -195, -213, -230, -248, -265, -282, -300, -317, -334, 
    -352, -368, -385, -402, -419, -436, -452, -469, -485, -501, -517, -533, -549, -564, -580, -595, -610, -625, -640, -655, 
    -669, -684, -698, -711, -725, -738, -751, -764, -777, -790, -802, -814, -825, -837, -848, -859, -870, -880, -891, -901, 
    -910, -920, -929, -937, -946, -954, -962, -969, -976, -983, -989, -995, -1000, -1005, -1010, -1014, -1018, -1021, -1023, -1024, 
    -1024, -1024, -1023, -1021, -1018, -1014, -1010, -1005, -1000, -995, -989, -983, -976, -969, -962, -954, -946, -937, -929, -920, 
    -910, -901, -891, -880, -870, -859, -848, -837, -825, -814, -802, -790, -777, -764, -751, -738, -725, -711, -698, -684, 
    -669, -655, -640, -625, -610, -595, -580, -564, -549, -533, -517, -501, -485, -469, -452, -436, -419, -402, -385, -368, 
    -352, -334, -317, -300, -282, -265, -248, -230, -213, -195, -177, -160, -142, -124, -106, -89, -71, -53, -35, -17
};

static inline int fix_angle(int angle) {
    angle = angle % 360;
    if (angle < 0) angle += 360;
    return angle;
}

static inline int int_sin(int angle) {
    return sine_table[fix_angle(angle)];
}

static inline int int_cos(int angle) {
    return sine_table[fix_angle(angle + 90)];
}

// --- İNTERAKTİF INPUT PANELİ (X, Y, Z, Yaw, Pitch) ---
static int focused_field = -1; // -1: Hiçbiri, 0:X, 1:Y, 2:Z, 3:Yaw, 4:Pitch
static char input_buf_x[16] = "0";
static char input_buf_y[16] = "0";
static char input_buf_z[16] = "180";
static char input_buf_yaw[16] = "0";
static char input_buf_pitch[16] = "0";
static bool prev_left_click = false;

static int custom_atoi(const char *str) {
    int res = 0;
    int sign = 1;
    if (*str == '-') {
        sign = -1;
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res * sign;
}

static void custom_itoa(int val, char *buf, int max_len) {
    int i = 0;
    int is_negative = 0;
    if (val < 0) {
        is_negative = 1;
        val = -val;
    }
    if (val == 0) {
        buf[i++] = '0';
        buf[i] = '\0';
        return;
    }
    while (val > 0 && i < max_len - 1) {
        buf[i++] = (val % 10) + '0';
        val /= 10;
    }
    if (is_negative && i < max_len - 1) {
        buf[i++] = '-';
    }
    buf[i] = '\0';

    int start = 0, end = i - 1;
    while (start < end) {
        char temp = buf[start];
        buf[start] = buf[end];
        buf[end] = temp;
        start++;
        end--;
    }
}

// Input paneli ve klavye/fare etkileşimini yöneten fonksiyon
static void handle_ui_inputs(camera_t *cam) {
    bool left_click = (mouse_buttons & 0x01) != 0;
    int mx = (int32_t)mouse_x;
    int my = (int32_t)mouse_y;

    // Sol tık ile kutucuk seçimi (Satır 1: X, Y, Z -> Y: 75-95 | Satır 2: Yaw, Pitch -> Y: 105-125)
    if (left_click && !prev_left_click) {
        if (my >= 75 && my <= 95) {
            if (mx >= 10 && mx <= 90) focused_field = 0;      // X
            else if (mx >= 95 && mx <= 175) focused_field = 1;  // Y
            else if (mx >= 180 && mx <= 260) focused_field = 2; // Z
            else focused_field = -1;
        } 
        else if (my >= 105 && my <= 125) {
            if (mx >= 10 && mx <= 110) focused_field = 3;       // Yaw
            else if (mx >= 115 && mx <= 215) focused_field = 4; // Pitch
            else focused_field = -1;
        }
        else {
            focused_field = -1;
        }
    }
    prev_left_click = left_click;

    // Seçilen kutucuğa göre klavye girdilerini işle
    if (focused_field != -1) {
        char *active_buf = 
            (focused_field == 0) ? input_buf_x :
            (focused_field == 1) ? input_buf_y :
            (focused_field == 2) ? input_buf_z :
            (focused_field == 3) ? input_buf_yaw : input_buf_pitch;

        int len = 0;
        while (active_buf[len] != '\0') len++;

        static uint8_t last_handled_scancode = 0;
        uint8_t sc = keyboard_get_last_scancode();

        if (sc != 0 && sc != last_handled_scancode) {
            last_handled_scancode = sc;

            if (sc == 0x0E) { // Backspace
                if (len > 0) active_buf[len - 1] = '\0';
            } 
            else if (sc == 0x1C) { // Enter -> Değerleri kameraya uygula
                int val = custom_atoi(active_buf);
                if (focused_field == 0) cam->position.x = TO_FIXED(val);
                if (focused_field == 1) cam->position.y = TO_FIXED(val);
                if (focused_field == 2) cam->position.z = TO_FIXED(val);
                if (focused_field == 3) cam->yaw = val;
                if (focused_field == 4) cam->pitch = val;
                focused_field = -1;
            } 
            else {
                char c = 0;
                if (sc >= 0x02 && sc <= 0x0A) c = '1' + (sc - 0x02); // 1-9
                else if (sc == 0x0B) c = '0';
                else if (sc == 0x0C) c = '-'; // Eksi

                if (c != 0 && len < 10) {
                    active_buf[len] = c;
                    active_buf[len + 1] = '\0';
                }
            }
        } else if (sc == 0) {
            last_handled_scancode = 0;
        }
    } else {
        // Focus yoksa mevcut kamera değerlerini string buffer'lara aktar
        custom_itoa(FROM_FIXED(cam->position.x), input_buf_x, sizeof(input_buf_x));
        custom_itoa(FROM_FIXED(cam->position.y), input_buf_y, sizeof(input_buf_y));
        custom_itoa(FROM_FIXED(cam->position.z), input_buf_z, sizeof(input_buf_z));
        custom_itoa(cam->yaw, input_buf_yaw, sizeof(input_buf_yaw));
        custom_itoa(cam->pitch, input_buf_pitch, sizeof(input_buf_pitch));
    }
}

void renderer3d_init(void) {
    cursor_init();
}

static void draw_pixel(int x, int y, uint32_t color) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (x < 0 || (uint32_t)x >= width || y < 0 || (uint32_t)y >= height)
        return;

    fb_putpixel(x, y, color);
}

static void renderer3d_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs_val(x1 - x0);
    int dy = abs_val(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void project_point(int x, int y, int z, camera_t *cam, int *screen_x, int *screen_y) {
    int rel_x = x - FROM_FIXED(cam->position.x);
    int rel_y = y - FROM_FIXED(cam->position.y);
    int rel_z = z - FROM_FIXED(cam->position.z);

    int yaw = cam->yaw;
    int sin_yaw = int_sin(yaw);
    int cos_yaw = int_cos(yaw);

    int x_yaw = (rel_x * cos_yaw - rel_z * sin_yaw) / 1024;
    int z_yaw = (rel_x * sin_yaw + rel_z * cos_yaw) / 1024;

    int pitch = cam->pitch;
    int sin_pitch = int_sin(pitch);
    int cos_pitch = int_cos(pitch);

    int y_pitch = (rel_y * cos_pitch - z_yaw * sin_pitch) / 1024;
    int z_pitch = (rel_y * sin_pitch + z_yaw * cos_pitch) / 1024;

    if (z_pitch < 10) {
        *screen_x = -1;
        *screen_y = -1;
        return;
    }

    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();
    int fov_scale = 400; 

    int x_val = (x_yaw * fov_scale) / z_pitch;
    int y_val = (y_pitch * fov_scale) / z_pitch;

    *screen_x = ((int)width / 2) + x_val;
    *screen_y = ((int)height / 2) - y_val;
}

// Sol üst HUD ve İnteraktif Input Paneli (Pos + Rotasyon)
static void render_hud(camera_t *cam, int cube_x, int cube_y, int cube_z) {
    char hud_buf[128];

    gfx_draw_text(10, 10, 0x00FFFFFF, "LenorOS 3D Renderer HUD");

    int cx = FROM_FIXED(cam->position.x);
    int cy = FROM_FIXED(cam->position.y);
    int cz = FROM_FIXED(cam->position.z);

    snprintf(hud_buf, sizeof(hud_buf), "cam-pos: %d, %d, %d (Yaw: %d, Pitch: %d)", cx, cy, cz, cam->yaw, cam->pitch);
    gfx_draw_text(10, 30, 0x0000FF00, hud_buf);

    snprintf(hud_buf, sizeof(hud_buf), "cube-pos: %d, %d, %d", cube_x, cube_y, cube_z);
    gfx_draw_text(10, 50, 0x00FFFF00, hud_buf);

    // --- İNTERAKTİF INPUT KUTULARI (KONUM & ROTASYON) ---
    gfx_draw_text(10, 62, 0x00CCCCCC, "Konum Gir (Tikla, Yaz, Enter):");

    // X, Y, Z Kutuları
    uint32_t col_x = (focused_field == 0) ? 0x0000FFFF : 0x00555555;
    uint32_t col_y = (focused_field == 1) ? 0x0000FFFF : 0x00555555;
    uint32_t col_z = (focused_field == 2) ? 0x0000FFFF : 0x00555555;

    snprintf(hud_buf, sizeof(hud_buf), "X:%s", input_buf_x);
    gfx_draw_text(10, 78, col_x, hud_buf);

    snprintf(hud_buf, sizeof(hud_buf), "Y:%s", input_buf_y);
    gfx_draw_text(95, 78, col_y, hud_buf);

    snprintf(hud_buf, sizeof(hud_buf), "Z:%s", input_buf_z);
    gfx_draw_text(180, 78, col_z, hud_buf);

    // Yaw, Pitch Kutuları
    gfx_draw_text(10, 93, 0x00CCCCCC, "Rotasyon Gir (Yaw / Pitch):");

    uint32_t col_yaw = (focused_field == 3) ? 0x0000FFFF : 0x00555555;
    uint32_t col_pitch = (focused_field == 4) ? 0x0000FFFF : 0x00555555;

    snprintf(hud_buf, sizeof(hud_buf), "Yaw:%s", input_buf_yaw);
    gfx_draw_text(10, 108, col_yaw, hud_buf);

    snprintf(hud_buf, sizeof(hud_buf), "Pitch:%s", input_buf_pitch);
    gfx_draw_text(115, 108, col_pitch, hud_buf);
}

static void render_input_debug(void) {
    char debug_buf[128];
    uint32_t sw = fb_get_width();

    int start_x = (int)sw - 230;
    if (start_x < 10) start_x = 10;

    gfx_draw_text(start_x, 10, 0x0000FFFF, "[ INPUT DEBUG ]");

    int mx = (int)mouse_x;
    int my = (int)mouse_y;
    uint8_t mbtn = mouse_buttons;

    bool left_click  = (mbtn & 0x01) != 0;
    bool right_click = (mbtn & 0x02) != 0;

    snprintf(debug_buf, sizeof(debug_buf), "Mouse: X:%d Y:%d", mx, my);
    gfx_draw_text(start_x, 30, 0x00CCCCCC, debug_buf);

    snprintf(debug_buf, sizeof(debug_buf), "Btn: L:%d R:%d", left_click ? 1 : 0, right_click ? 1 : 0);
    gfx_draw_text(start_x, 50, right_click ? 0x00FF00FF : 0x00CCCCCC, debug_buf);

    uint8_t scancode = keyboard_get_last_scancode();
    snprintf(debug_buf, sizeof(debug_buf), "Key: 0x%X", (unsigned int)scancode);
    gfx_draw_text(start_x, 70, 0x00FFFF00, debug_buf);
}

void renderer3d_draw_frame(camera_t *cam) {
    if (!cam) return;

    static bool initial_set = false;
    if (!initial_set) {
        camera_init(cam);
        initial_set = true;
    }

    handle_ui_inputs(cam);
    camera_update(cam, mouse_x, mouse_y, mouse_buttons);

    fb_clear(0x000000);

    int cube_x = 0;
    int cube_y = 0;
    int cube_z = 180;

    int vertices[8][3] = {
        {cube_x - 40, cube_y - 40, cube_z - 40},
        {cube_x + 40, cube_y - 40, cube_z - 40},
        {cube_x + 40, cube_y + 40, cube_z - 40},
        {cube_x - 40, cube_y + 40, cube_z - 40},
        {cube_x - 40, cube_y - 40, cube_z + 40},
        {cube_x + 40, cube_y - 40, cube_z + 40},
        {cube_x + 40, cube_y + 40, cube_z + 40},
        {cube_x - 40, cube_y + 40, cube_z + 40}
    };

    int screen_x[8], screen_y[8];

    for (int i = 0; i < 8; i++) {
        project_point(vertices[i][0], vertices[i][1], vertices[i][2], cam, &screen_x[i], &screen_y[i]);
    }

    int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7}
    };

    for (int i = 0; i < 12; i++) {
        int p0 = edges[i][0];
        int p1 = edges[i][1];

        if (screen_x[p0] != -1 && screen_y[p0] != -1 && 
            screen_x[p1] != -1 && screen_y[p1] != -1) {
            renderer3d_draw_line(screen_x[p0], screen_y[p0], screen_x[p1], screen_y[p1], 0x00FFFFFF);
        }
    }

    render_hud(cam, cube_x, cube_y, cube_z);
    render_input_debug();

    cursor_show();

    fb_swap();
}