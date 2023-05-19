#include "flash_handler.h"

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
//#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

char *highscore_file_name = "highscore.txt";

//LOG_MODULE_REGISTER(flash_handler);

#define PARTITION_NODE DT_NODELABEL(lfs)

#define MAX_PATH_LEN 255

#if DT_NODE_EXISTS(PARTITION_NODE)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);
#else /* PARTITION_NODE */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &storage,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.mnt_point = "/lfs",
};
#endif /* PARTITION_NODE */

	struct fs_mount_t *mp =
#if DT_NODE_EXISTS(PARTITION_NODE)
		&FS_FSTAB_ENTRY(PARTITION_NODE)
#else
		&lfs_storage_mnt
#endif
		;

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

    fs_file_t_init(&file);
    err = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
    if (err < 0) {
        printk("FAIL: open %s: %d", fname, err);
        return err;
    }
#if 0
	err = fs_read(&file, name, HIGHSCORE_STR_LEN_MAX);
	if (err <= 0) {
		printk("FAIL: read %s: [rd:%d]", fname, err);
        return err;
    }
    //LOG_WRN("Read done");
    err = fs_read(&file, email, HIGHSCORE_STR_LEN_MAX);
	if (err <= 0) {
		printk("FAIL: read %s: [rd:%d]", fname, err);
        return err;
    }
    //LOG_WRN("Read done");
    index_read++;
    *pos = index_read;
    #endif
    err = fs_close(&file);
    if (err) {
        printk("FAIL: close (err %i)", err);
        return err;
    }
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
#if 0
    ret = fs_write(&file, name, HIGHSCORE_STR_LEN_MAX);
    if (ret < 0) {
        printk("FAIL: write name (err %i)", ret);
        return ret;
    }
    //LOG_WRN("write done");
    ret = fs_write(&file, email, HIGHSCORE_STR_LEN_MAX);
    if (ret < 0) {
        printk("FAIL: write email (err %i)", ret);
        return ret;
    }
    //LOG_WRN("write done");
#endif
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