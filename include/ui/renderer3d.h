#ifndef RENDERER3D_H
#define RENDERER3D_H

#include <ui/camera.h>

void renderer3d_init(void);
void renderer3d_draw_frame(camera_t *cam);

#endif