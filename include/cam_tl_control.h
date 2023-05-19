#ifndef __CAM_TL_CONTROL_H
#define __CAM_TL_CONTROL_H

#include <zephyr/kernel.h>
#include <zephyr/types.h>

typedef struct {
} cam_tl_control_config_t;

int cam_tl_control_init(void);

void cam_tl_control_take_picture(void);

#endif