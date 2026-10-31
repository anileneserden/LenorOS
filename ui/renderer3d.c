#include <ui/renderer3d.h>
#include <ui/camera.h>
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

void renderer3d_init(void) {
    // Başlangıç ayarları
}

// Pixel çizim fonksiyonu
static void draw_pixel(int x, int y, uint32_t color) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (x < 0 || (uint32_t)x >= width || y < 0 || (uint32_t)y >= height)
        return;

    fb_putpixel(x, y, color);
}

// Bresenham Çizgi Çizme Algoritması
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
    // 1. Kameraya göre göreceli konum (Translation)
    int rel_x = x - FROM_FIXED(cam->position.x);
    int rel_y = y - FROM_FIXED(cam->position.y);
    int rel_z = z - FROM_FIXED(cam->position.z);

    // 2. YAW ROTASYONU (Y ekseni etrafında)
    int yaw = cam->yaw;
    int sin_yaw = int_sin(yaw);
    int cos_yaw = int_cos(yaw);

    int x_yaw = (rel_x * cos_yaw - rel_z * sin_yaw) / 1024;
    int z_yaw = (rel_x * sin_yaw + rel_z * cos_yaw) / 1024;

    // 3. PITCH ROTASYONU (X ekseni etrafında)
    int pitch = cam->pitch;
    int sin_pitch = int_sin(pitch);
    int cos_pitch = int_cos(pitch);

    int y_pitch = (rel_y * cos_pitch - z_yaw * sin_pitch) / 1024;
    int z_pitch = (rel_y * sin_pitch + z_yaw * cos_pitch) / 1024;

    // 4. NEAR CLIPPING (Kameranın çok yaklaşmasını veya arkada kalmasını engelle)
    // Z derinliği 10 birimden küçükse veya negatifse (kameranın arkasındaysa) çizme!
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

// Sol üst HUD Bilgileri
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
}

// Sağ üst Input Debug Overlay
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

// Her karede çağrılacak ana çizim döngüsü
void renderer3d_draw_frame(camera_t *cam) {
    if (!cam) return;

    // Kamera ilk kez çalıştırılıyorsa başlat
    static bool initial_set = false;
    if (!initial_set) {
        camera_init(cam);
        initial_set = true;
    }

    // Girdi verilerini kamera modülüne aktar ve güncelle
    uint8_t scancode = keyboard_get_last_scancode();
    camera_update(cam, mouse_x, mouse_y, mouse_buttons);

    fb_clear(0x000000);

    // Küpün merkez koordinatları
    int cube_x = 0;
    int cube_y = 0;
    int cube_z = 180;

    // Küpün 8 köşe noktası
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
        {0,1}, {1,2}, {2,3}, {3,0}, // Ön yüz
        {4,5}, {5,6}, {6,7}, {7,4}, // Arka yüz
        {0,4}, {1,5}, {2,6}, {3,7}  // Bağlantılar
    };

    for (int i = 0; i < 12; i++) {
        int p0 = edges[i][0];
        int p1 = edges[i][1];

        if (screen_x[p0] != -1 && screen_y[p0] != -1 && 
            screen_x[p1] != -1 && screen_y[p1] != -1) {
            renderer3d_draw_line(screen_x[p0], screen_y[p0], screen_x[p1], screen_y[p1], 0x00FFFFFF);
        }
    }

    // Sol üst HUD ve Sağ üst Input Debug panelleri
    render_hud(cam, cube_x, cube_y, cube_z);
    render_input_debug();

    fb_swap();
}