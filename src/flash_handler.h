#ifndef __FLASH_HANDLER_H
#define __FLASH_HANDLER_H

#include <zephyr/kernel.h>
#include "app_settings.h"

int flash_handler_init(void);

int flash_handler_read(app_settings_t *settings);

int flash_handler_write(app_settings_t *settings);

int flash_handler_erase(void);

#endif
