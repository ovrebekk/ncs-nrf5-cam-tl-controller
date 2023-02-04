#ifndef __CAM_TL_CONTROL_H
#define __CAM_TL_CONTROL_H

#include <zephyr/kernel.h>
#include <zephyr/types.h>

typedef struct {
	uint32_t pin_focus;
	uint32_t pin_shutter;
} cam_tl_control_config_t;

void cam_tl_control_init(cam_tl_control_config_t *config);

void cam_tl_control_take_picture(void);

#endif