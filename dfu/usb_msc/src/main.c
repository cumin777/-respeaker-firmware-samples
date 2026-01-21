#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/devicetree.h>

#include <zephyr/fs/fs.h>
#include <ff.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/storage/disk_access.h>

#include <string.h>

LOG_MODULE_REGISTER(usb_msc_flash, LOG_LEVEL_INF);

/*
 * USB VID/PID must be assigned to your organization for production.
 * These are placeholder values for local development.
 */
#define APP_USB_VID 0xCAFE
#define APP_USB_PID 0x0001

USBD_DESC_LANG_DEFINE(app_lang);
USBD_DESC_STRING_DEFINE(app_mfr, "NCS", 1);
USBD_DESC_STRING_DEFINE(app_product, "USB MSC (flash)", 2);
USBD_DESC_STRING_DEFINE(app_sn, "0000000000000001", 3);

USBD_DESC_CONFIG_DEFINE(app_fs_cfg_desc, "FS Config");
USBD_CONFIGURATION_DEFINE(app_fs_config, 0, 100, &app_fs_cfg_desc);

static const char *const app_blocklist[] = {
	/* Keep DFU out by default. */
	"dfu_dfu",
	NULL,
};

USBD_DEVICE_DEFINE(app_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   APP_USB_VID, APP_USB_PID);

/* Disk name must match the devicetree zephyr,flash-disk disk-name. */
USBD_DEFINE_MSC_LUN(qspi_lun, "QSPI", "NCS", "FLASHDISK", "1.0");

#define MSC_DISK_NAME "QSPI"
#define FAT_MNT_POINT "/QSPI:"
#define FW_FILENAME   "firmware.bin"

static FATFS fat_fs;
static struct fs_mount_t fat_mount = {
	.type = FS_FATFS,
	.mnt_point = FAT_MNT_POINT,
	.fs_data = &fat_fs,
	.storage_dev = (void *)MSC_DISK_NAME,
	/* Default to NO_FORMAT for safety; we may temporarily clear it at boot. */
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS | FS_MOUNT_FLAG_NO_FORMAT,
};

static struct usbd_context *g_usbd;

static struct k_work fw_scan_work;

static const struct gpio_dt_spec btn1 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {0});
static struct gpio_callback btn1_cb;

#define STORAGE_PARTITION DT_NODELABEL(qspi_msc_partition)
#if !DT_NODE_EXISTS(STORAGE_PARTITION)
#error "qspi_msc_partition is not defined in devicetree"
#endif

static struct usbd_context *usb_init(void)
{
	int err;

	err = usbd_add_descriptor(&app_usbd, &app_lang);
	if (err) {
		LOG_ERR("Failed to add LANG descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&app_usbd, &app_mfr);
	if (err) {
		LOG_ERR("Failed to add MFR descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&app_usbd, &app_product);
	if (err) {
		LOG_ERR("Failed to add PRODUCT descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&app_usbd, &app_sn);
	if (err) {
		LOG_ERR("Failed to add SN descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_configuration(&app_usbd, USBD_SPEED_FS, &app_fs_config);
	if (err) {
		LOG_ERR("Failed to add FS configuration (%d)", err);
		return NULL;
	}

	err = usbd_register_all_classes(&app_usbd, USBD_SPEED_FS, 1, app_blocklist);
	if (err) {
		LOG_ERR("Failed to register USB classes (%d)", err);
		return NULL;
	}

	/* Let interface descriptors define class/subclass/protocol. */
	usbd_device_set_code_triple(&app_usbd, USBD_SPEED_FS, 0, 0, 0);

	err = usbd_init(&app_usbd);
	if (err) {
		LOG_ERR("Failed to init USBD (%d)", err);
		return NULL;
	}

	return &app_usbd;
}

static int setup_flash_partition(void)
{
	const struct device *flash_dev = DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(STORAGE_PARTITION));
	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device not ready");
		return -ENODEV;
	}

	LOG_INF("MSC backing store: QSPI (qspi_msc_partition)");
	LOG_INF("- flash: %s", flash_dev->name);
	LOG_INF("- offset: 0x%lx", (unsigned long)DT_REG_ADDR(STORAGE_PARTITION));
	LOG_INF("- size: %lu bytes", (unsigned long)DT_REG_SIZE(STORAGE_PARTITION));

	return 0;
}

static void scan_root_for_firmware_file(void)
{
	int err;
	struct fs_dir_t dir;
	struct fs_dirent entry;
	char path[64];

	snprintk(path, sizeof(path), "%s/%s", FAT_MNT_POINT, FW_FILENAME);
	err = fs_stat(path, &entry);
	if (err == 0 && entry.type == FS_DIR_ENTRY_FILE) {
		LOG_INF("Found firmware file: %s (%u bytes)", path, (unsigned int)entry.size);
		return;
	}

	/* Fallback: list root and look for any *.bin */
	fs_dir_t_init(&dir);
	err = fs_opendir(&dir, FAT_MNT_POINT);
	if (err) {
		LOG_WRN("fs_opendir(%s) failed: %d", FAT_MNT_POINT, err);
		return;
	}

	while (true) {
		err = fs_readdir(&dir, &entry);
		if (err) {
			LOG_WRN("fs_readdir failed: %d", err);
			break;
		}

		if (entry.name[0] == '\0') {
			break;
		}

		if (entry.type != FS_DIR_ENTRY_FILE) {
			continue;
		}

		const char *ext = strrchr(entry.name, '.');
		if (ext && (strcmp(ext, ".bin") == 0 || strcmp(ext, ".BIN") == 0)) {
			LOG_INF("Found candidate firmware file: %s/%s (%u bytes)",
				FAT_MNT_POINT, entry.name, (unsigned int)entry.size);
		}
	}

	(void)fs_closedir(&dir);
}

static void firmware_scan_once(bool cycle_usb)
{
	int err;
	uint32_t saved_flags = fat_mount.flags;

	/*
	 * If the QSPI region is blank, FatFS will return FR_NO_FILESYSTEM (-ENODEV).
	 * We allow auto-mkfs only on the boot-time scan (before USB is enabled),
	 * and keep NO_FORMAT set during user-triggered scans to avoid wiping data.
	 */
	if (!cycle_usb) {
		fat_mount.flags &= ~FS_MOUNT_FLAG_NO_FORMAT;
	}

	if (cycle_usb && g_usbd) {
		LOG_INF("Disabling USB for safe FAT mount...");
		err = usbd_disable(g_usbd);
		if (err) {
			LOG_WRN("usbd_disable failed: %d", err);
		}
		k_msleep(200);
	}

	err = fs_mount(&fat_mount);
	if (err) {
		LOG_WRN("FAT mount failed (%d). If first use, format the drive as FAT on PC.", err);
		goto out;
	}

	LOG_INF("FAT mounted at %s", FAT_MNT_POINT);
	scan_root_for_firmware_file();

	err = fs_unmount(&fat_mount);
	if (err) {
		LOG_WRN("fs_unmount failed: %d", err);
	}

out:
	fat_mount.flags = saved_flags;
	if (cycle_usb && g_usbd) {
		k_msleep(200);
		LOG_INF("Re-enabling USB...");
		err = usbd_enable(g_usbd);
		if (err) {
			LOG_WRN("usbd_enable failed: %d", err);
		}
	}
}

static void fw_scan_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	firmware_scan_once(true);
}

static void btn1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	(void)k_work_submit(&fw_scan_work);
}

static int btn1_init(void)
{
	int err;

	if (!device_is_ready(btn1.port)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&btn1, GPIO_INPUT);
	if (err) {
		return err;
	}

	gpio_init_callback(&btn1_cb, btn1_pressed, BIT(btn1.pin));
	err = gpio_add_callback(btn1.port, &btn1_cb);
	if (err) {
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&btn1, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		(void)gpio_remove_callback(btn1.port, &btn1_cb);
		return err;
	}

	return 0;
}

int main(void)
{
	int err;
	struct usbd_context *ctx;

	LOG_INF("USB MSC (QSPI) starting");

	err = setup_flash_partition();
	if (err) {
		LOG_ERR("Flash partition setup failed: %d", err);
		return 0;
	}

	/*
	 * Ensure the underlying disk driver is initialized before the host
	 * starts sending SCSI READ CAPACITY/INQUIRY over bulk endpoints.
	 */
	err = disk_access_init(MSC_DISK_NAME);
	if (err) {
		LOG_ERR("disk_access_init(%s) failed: %d", MSC_DISK_NAME, err);
		return 0;
	}

	err = disk_access_status(MSC_DISK_NAME);
	LOG_INF("disk_access_status(%s) = %d", MSC_DISK_NAME, err);

	/*
	 * Best-effort scan at boot (before USB is enabled).
	 * This lets you: copy firmware to the drive, then reset the board.
	 */
	firmware_scan_once(false);

	ctx = usb_init();
	if (ctx == NULL) {
		LOG_ERR("USB init failed");
		return 0;
	}
	g_usbd = ctx;

	err = usbd_enable(ctx);
	if (err) {
		LOG_ERR("Failed to enable USBD: %d", err);
		return 0;
	}

	LOG_INF("USB enabled. Connect to PC; it should enumerate as a mass storage device.");

	k_work_init(&fw_scan_work, fw_scan_work_handler);

	err = btn1_init();
	if (err) {
		LOG_WRN("BTN1 init failed: %d (button trigger disabled)", err);
	} else {
		LOG_INF("Press BTN1 to scan FAT for '%s' (use 'Safely remove' first)", FW_FILENAME);
	}

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
