#include <ui/camera.h>
#include <stdbool.h>

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

int int_sin(int angle) {
    return sine_table[fix_angle(angle)];
}

int int_cos(int angle) {
    return sine_table[fix_angle(angle + 90)];
}

void camera_init(camera_t *cam) {
    if (!cam) return;
    cam->position.x = 0;
    cam->position.y = 0;
    cam->position.z = 0;
    cam->yaw = 0;
    cam->pitch = 0;
}

void camera_update(camera_t *cam, int32_t mouse_x, int32_t mouse_y, uint8_t mouse_buttons) {
    if (!cam) return;

    // --- 1. MOUSE LOOK (SAĞ TIK İLE YAW & PITCH DEĞİŞTİRME) ---
    static int32_t last_mouse_x = 0;
    static int32_t last_mouse_y = 0;
    static bool right_click_was_pressed = false;
    bool right_click = (mouse_buttons & 0x02) != 0;

    if (right_click) {
        if (!right_click_was_pressed) {
            last_mouse_x = mouse_x;
            last_mouse_y = mouse_y;
            right_click_was_pressed = true;
        }

        int32_t delta_x = mouse_x - last_mouse_x;
        int32_t delta_y = mouse_y - last_mouse_y;
        int sensitivity = 1; 

        cam->yaw += delta_x * sensitivity;
        cam->pitch -= delta_y * sensitivity;

        last_mouse_x = mouse_x;
        last_mouse_y = mouse_y;
    } else {
        right_click_was_pressed = false;
    }

    // --- 2. ANLIK TUŞ KONTROLÜ İLE 3D HAREKET MANTIĞI ---
    int move_speed = 1; // Hız aşırı hızlı gelirse burayı düşürebilirsin (örn: 1 yerine 50-100 yapabilirsin)
    
    int yaw = cam->yaw;
    int pitch = cam->pitch;
    
    int sin_yaw = int_sin(yaw);
    int cos_yaw = int_cos(yaw);
    int sin_pitch = int_sin(pitch);
    int cos_pitch = int_cos(pitch);

    int forward_x = (sin_yaw * cos_pitch) / 1024;
    int forward_y = (-sin_pitch);
    int forward_z = (cos_yaw * cos_pitch) / 1024;

    int right_x = cos_yaw;
    int right_z = -sin_yaw;

    // Scancode set 1 karşılıkları: W=0x11, S=0x1F, A=0x1E, D=0x20
    if (keyboard_is_key_pressed(0x11)) {      // W -> İleri
        cam->position.x += TO_FIXED(forward_x * move_speed) / 1024;
        cam->position.y += TO_FIXED(forward_y * move_speed) / 1024;
        cam->position.z += TO_FIXED(forward_z * move_speed) / 1024;
    }
    if (keyboard_is_key_pressed(0x1F)) {      // S -> Geri
        cam->position.x -= TO_FIXED(forward_x * move_speed) / 1024;
        cam->position.y -= TO_FIXED(forward_y * move_speed) / 1024;
        cam->position.z -= TO_FIXED(forward_z * move_speed) / 1024;
    }
    if (keyboard_is_key_pressed(0x1E)) {      // A -> Sol
        cam->position.x -= TO_FIXED(right_x * move_speed) / 1024;
        cam->position.z -= TO_FIXED(right_z * move_speed) / 1024;
    }
    if (keyboard_is_key_pressed(0x20)) {      // D -> Sağ
        cam->position.x += TO_FIXED(right_x * move_speed) / 1024;
        cam->position.z += TO_FIXED(right_z * move_speed) / 1024;
    }
}