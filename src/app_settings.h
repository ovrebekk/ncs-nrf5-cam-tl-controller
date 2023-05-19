#ifndef __APP_SETTINGS_H
#define __APP_SETTINGS_H

#include <zephyr/kernel.h>

typedef struct {
    int picture_interval_s;
    int pic_cap_start_hour;
    int pic_cap_start_min;
    int pic_cap_end_hour;
    int pic_cap_end_min;
    bool wday_on_map[7];
} app_settings_t;

#endif
