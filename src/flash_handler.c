#include "flash_handler.h"

#include <stdio.h>

#include <zephyr/device.h>
//#include <zephyr/fs/fs.h>
//#include <zephyr/logging/log.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>

//LOG_MODULE_REGISTER(flash_handler);

#define NVS_PARTITION		storage_partition
#define NVS_PARTITION_DEVICE	FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET	FIXED_PARTITION_OFFSET(NVS_PARTITION)

static struct nvs_fs nvs_fs;

int flash_handler_init(void)
{
	int err;
	struct flash_pages_info info;

	/* define the nvs file system by settings with:
	 *	sector_size equal to the pagesize,
	 *	3 sectors
	 *	starting at NVS_PARTITION_OFFSET
	 */
	nvs_fs.flash_device = NVS_PARTITION_DEVICE;
	if (!device_is_ready(nvs_fs.flash_device)) {
		printk("Flash device %s is not ready\n", nvs_fs.flash_device->name);
		return -ENFILE;
	}
	nvs_fs.offset = NVS_PARTITION_OFFSET;
	err = flash_get_page_info_by_offs(nvs_fs.flash_device, nvs_fs.offset, &info);
	if (err) {
		printk("Unable to get page info\n");
		return -ENFILE;
	}
	nvs_fs.sector_size = info.size;
	nvs_fs.sector_count = 3U;

    err = nvs_mount(&nvs_fs);
	if (err) {
		printk("Flash Init failed\n");
		return -ENFILE;
	}

    return 0;
}

int flash_handler_read(app_settings_t *settings)
{
    static app_settings_t local_settings;

    ssize_t read_size = nvs_read(&nvs_fs, 26, &local_settings, sizeof(app_settings_t));
    if (read_size != sizeof(app_settings_t)) {
        return -ENFILE;
    }

    // If all the operations were successful, copy the read settings into the out pointer
    *settings = local_settings;
    return 0;
}

int flash_handler_write(app_settings_t *settings)
{
    ssize_t written_size = nvs_write(&nvs_fs, 26, settings, sizeof(app_settings_t));
    if (written_size == 0) {
        // Identical settings, nothing written to flash
        return 0;
    } else if (written_size != sizeof(app_settings_t)) {
        return -ENFILE;
    }

    return 0;
}
