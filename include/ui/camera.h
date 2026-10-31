#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

typedef int fixed_t;

#define TO_FIXED(x) ((fixed_t)((x) * 65536))
#define FROM_FIXED(x) ((x) >> 16)

typedef struct {
    fixed_t x, y, z;
} vec3_t;

typedef struct {
    vec3_t position;
    int yaw;
    int pitch;
} camera_t;

void camera_init(camera_t *cam);
void camera_update(camera_t *cam, int32_t mouse_x, int32_t mouse_y, uint8_t mouse_buttons);

#endif