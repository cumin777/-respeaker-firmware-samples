/*
 * Custom SD SPI SDMMC Driver - Integration Example
 *
 * Copyright (c) 2024, Custom Driver Module
 * SPDX-License-Identifier: Apache-2.0
 *
 * This example demonstrates how to use the custom SD card driver
 * with LittleFS file system for the reSpeaker project.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(sd_card_example, CONFIG_LOG_DEFAULT_LEVEL);

/* File system mount point */
#define SD_CARD_MOUNT_POINT "/sd"

/* Audio buffer size for recording */
#define AUDIO_BUFFER_SIZE 4096
#define WRITE_INTERVAL_MS 500

static struct fs_mount_t mp = {
	.type = FS_LITTLEFS,
	.mnt_point = SD_CARD_MOUNT_POINT,
	.fs_data = NULL,
};

/* SD card device (adjust node label based on your DTS) */
static const char *sd_dev_name = "SPI_1";

/**
 * @brief Get SD card device from node label
 */
static const struct device *sd_get_device(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(sd_spi_sdmmc))
	return DEVICE_DT_GET(DT_NODELABEL(sd_spi_sdmmc));
#else
	LOG_ERR("SD card device not defined in device tree");
	return NULL;
#endif
}

/**
 * @brief Format SD card with LittleFS
 */
static int sd_format_card(void)
{
	const struct device *dev = sd_get_device();
	struct fs_statvfs stat;
	int rc;

	if (!dev) {
		return -ENODEV;
	}

	LOG_INF("Unmounting SD card...");
	fs_unmount(&mp);

	LOG_INF("Formatting SD card with LittleFS...");
	rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)dev, NULL, 0);
	if (rc != 0) {
		LOG_ERR("Format failed: %d", rc);
		return rc;
	}

	LOG_INF("Mounting SD card...");
	rc = fs_mount(&mp);
	if (rc != 0) {
		LOG_ERR("Mount failed: %d", rc);
		return rc;
	}

	/* Get file system statistics */
	rc = fs_statvfs(SD_CARD_MOUNT_POINT, &stat);
	if (rc == 0) {
		LOG_INF("Total space: %lu KB, Free space: %lu KB",
			(stat.f_bsize * stat.f_blocks) / 1024,
			(stat.f_bsize * stat.f_bfree) / 1024);
	}

	return 0;
}

/**
 * @brief Write audio data to SD card
 */
static int sd_write_audio(const char *filename, uint8_t *data, size_t len)
{
	struct fs_file_t file;
	ssize_t bytes_written;
	int rc;

	LOG_DBG("Writing audio to %s", filename);

	/* Open file for writing */
	rc = fs_open(&file, filename, FS_O_CREATE | FS_O_WRITE);
	if (rc != 0) {
		LOG_ERR("Failed to open file: %d", rc);
		return rc;
	}

	/* Write audio data */
	bytes_written = fs_write(&file, data, len);
	if (bytes_written < 0) {
		LOG_ERR("Failed to write: %d", bytes_written);
		fs_close(&file);
		return bytes_written;
	}

	/* Sync to ensure data is written to card */
	rc = fs_sync(&file);
	if (rc != 0) {
		LOG_WRN("Sync failed: %d", rc);
	}

	/* Close file */
	rc = fs_close(&file);
	if (rc != 0) {
		LOG_ERR("Failed to close file: %d", rc);
		return rc;
	}

	LOG_INF("Wrote %d bytes to %s", bytes_written, filename);
	return 0;
}

/**
 * @brief Generate unique filename for recording
 */
static void sd_generate_filename(char *filename, size_t len)
{
	static uint32_t counter = 0;

	snprintf(filename, len, "%s/audio_%04lu.raw", SD_CARD_MOUNT_POINT, counter++);
}

/**
 * @brief List files on SD card
 */
static int sd_list_files(void)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;
	int rc;
	uint32_t file_count = 0;

	LOG_INF("Listing files in %s:", SD_CARD_MOUNT_POINT);

	rc = fs_opendir(&dir, SD_CARD_MOUNT_POINT);
	if (rc != 0) {
		LOG_ERR("Failed to open directory: %d", rc);
		return rc;
	}

	while (true) {
		rc = fs_readdir(&dir, &entry);
		if (rc != 0 || entry.name[0] == '\0') {
			break;
		}

		if (entry.type == FS_DIR_ENTRY_FILE) {
			LOG_INF("  File: %-32s  Size: %lu bytes",
				 entry.name, entry.size);
			file_count++;
		} else if (entry.type == FS_DIR_ENTRY_DIR) {
			LOG_INF("  Dir:  %s", entry.name);
		}
	}

	fs_closedir(&dir);

	LOG_INF("Total files: %u", file_count);
	return 0;
}

/**
 * @brief Main application entry point
 */
int main(void)
{
	const struct device *sd_dev;
	char filename[64];
	uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
	int rc;
	uint32_t sample_count = 0;

	LOG_INF("Custom SD Card Example - reSpeaker Project");

	/* Get SD card device */
	sd_dev = sd_get_device();
	if (!sd_dev) {
		LOG_ERR("SD card device not found");
		return -ENODEV;
	}

	LOG_INF("SD card device: %s", sd_dev->name);

	/* Initialize disk access subsystem */
	rc = disk_access_init(sd_dev_name);
	if (rc != 0) {
		LOG_ERR("Disk access init failed: %d", rc);
		return rc;
	}

	/* Wait for SD card to be ready */
	k_msleep(100);

	/* Mount file system */
	rc = fs_mount(&mp);
	if (rc != 0) {
		LOG_WRN("Mount failed (formatting card): %d", rc);
		rc = sd_format_card();
		if (rc != 0) {
			LOG_ERR("Failed to format and mount: %d", rc);
			return rc;
		}
	}

	LOG_INF("SD card mounted at %s", SD_CARD_MOUNT_POINT);

	/* List existing files */
	sd_list_files();

	/* Simulate audio recording */
	LOG_INF("Simulating audio recording...");

	while (true) {
		/* Generate audio data (in real app, this comes from PDM) */
		memset(audio_buffer, 0xAA + (sample_count & 0xFF), AUDIO_BUFFER_SIZE);
		sample_count++;

		/* Generate filename */
		sd_generate_filename(filename, sizeof(filename));

		/* Write audio data to SD card */
		rc = sd_write_audio(filename, audio_buffer, AUDIO_BUFFER_SIZE);
		if (rc != 0) {
			LOG_ERR("Write failed: %d", rc);
			break;
		}

		/* Delay between writes (simulating recording interval) */
		k_msleep(WRITE_INTERVAL_MS);

		/* Stop after 10 samples for this example */
		if (sample_count >= 10) {
			break;
		}
	}

	/* List files after recording */
	LOG_INF("Recording complete. Files on SD card:");
	sd_list_files();

	LOG_INF("SD Card Example finished");
	return 0;
}

/*
 * Kconfig settings for this example:
 *
 * CONFIG_CUSTOM_SD_SPI_SDMMC=y
 * CONFIG_DISK_ACCESS=y
 * CONFIG_FILE_SYSTEM=y
 * CONFIG_LITTLEFS=y
 * CONFIG_FS_LITTLEFS_FCNTL_MAX_OPEN_FILES=4
 * CONFIG_DISK_DRIVER_SDMMC=y
 * CONFIG_LOG=y
 * CONFIG_LOG_MODE_IMMEDIATE=y
 */
