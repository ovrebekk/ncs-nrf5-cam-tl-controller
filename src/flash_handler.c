#include "flash_handler.h"

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
//#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

char *highscore_file_name = "highscore.txt";

//LOG_MODULE_REGISTER(flash_handler);

#define PARTITION_NODE DT_NODELABEL(lfs1)

#define MAX_PATH_LEN 255

FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);

struct fs_mount_t *mp = &FS_FSTAB_ENTRY(PARTITION_NODE);

static char fname[MAX_PATH_LEN];
int flash_handler_init(void)
{
	struct fs_statvfs sbuf;
	int err;

	printk("Initializing littlefs\n");

#if 0
	rc = littlefs_mount(mp);
	if (rc < 0) {
		return;
	}
#endif

	snprintf(fname, sizeof(fname), "%s/%s", mp->mnt_point, highscore_file_name);

	err = fs_statvfs(mp->mnt_point, &sbuf);
	if (err < 0) {
		printk("FAIL: statvfs: %d\n", err);
		return -1;
	}

	printk("%s: bsize = %lu ; frsize = %lu ;"
		   " blocks = %lu ; bfree = %lu\n",
		   mp->mnt_point,
		   sbuf.f_bsize, sbuf.f_frsize,
		   sbuf.f_blocks, sbuf.f_bfree);

    return 0;
}
static struct fs_file_t file;

int flash_handler_read(app_settings_t *settings)
{
	int err;
    static app_settings_t local_settings;

    fs_file_t_init(&file);
    err = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
    if (err < 0) {
        printk("FAIL: open %s: %d\n", fname, err);
        return err;
    }

	err = fs_read(&file, (void *)&local_settings, sizeof(app_settings_t));
	if (err < sizeof(app_settings_t)) {
		printk("FAIL: read %s: [rd:%d]\n", fname, err);
        return -EINVAL;
    }

    err = fs_close(&file);
    if (err) {
        printk("FAIL: close (err %i)\n", err);
        return err;
    }

    // If all the operations were successful, copy the read settings into the out pointer
    *settings = local_settings;
    return 0;
}

int flash_handler_write(app_settings_t *settings)
{
    int ret;

    fs_file_t_init(&file);
    ret = fs_open(&file, fname, FS_O_CREATE | FS_O_APPEND | FS_O_RDWR);
    if (ret < 0) {
        printk("FAIL: open %s: %d", fname, ret);
        return ret;
    }

    ret = fs_write(&file, (void*)settings, sizeof(app_settings_t));
    if (ret < 0) {
        printk("FAIL: write name (err %i)", ret);
        return ret;
    }

    ret = fs_close(&file);
    if (ret) {
        printk("FAIL: close (err %i)", ret);
        return ret;
    }

    return 0;
}

int flash_handler_erase(void)
{
    int ret; 
    ret = fs_unlink(fname);
    if (ret < 0) {
        printk("Error deleting file");
        return ret;
    }
    
    return 0;
}