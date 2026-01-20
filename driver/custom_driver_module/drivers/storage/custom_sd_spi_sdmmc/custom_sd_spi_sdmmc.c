/*
 * Copyright (c) 2024, Custom Driver Module
 * SPDX-License-Identifier: Apache-2.0
 *
 * Custom SD SPI SDMMC Driver
 * Based on SD 2.0 specification and reference implementation from Longsto
 */

#include <zephyr/logging/log.h>
#include "custom_sd_spi_sdmmc.h"

LOG_MODULE_REGISTER(sd_spi_sdmmc, CONFIG_DISK_DRIVER_SDMMC_LOG_LEVEL);

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @brief Select SD card by pulling CS low
 */
static inline void sd_spi_select(const struct device *dev)
{
	const struct sd_spi_config *config = dev->config;
	gpio_pin_set_dt(&config->cs, 0);
}

/**
 * @brief Deselect SD card by pulling CS high
 */
static inline void sd_spi_deselect(const struct device *dev)
{
	const struct sd_spi_config *config = dev->config;
	gpio_pin_set_dt(&config->cs, 1);
	/* Send extra clock cycles as per SD spec */
	spi_write_dt(&config->bus, (uint8_t[]){0xFF}, 1, NULL, 0);
}

/**
 * @brief Wait for SD card to be ready
 * @return 0 on success, 1 on timeout
 */
static int sd_spi_wait_ready(const struct device *dev)
{
	const struct sd_spi_config *config = dev->config;
	uint8_t response;
	uint32_t retries = SD_BUSY_RETRY_COUNT;
	const uint8_t dummy = 0xFF;

	do {
		spi_write_dt(&config->bus, &dummy, 1, &response, 1);
		if (response == 0xFF) {
			return 0;
		}
		k_busy_wait(1);
	} while (retries--);

	LOG_WRN("Card not ready");
	return 1;
}

/**
 * @brief Send SPI byte and receive response
 */
static uint8_t sd_spi_xfer_byte(const struct device *dev, uint8_t data)
{
	const struct sd_spi_config *config = dev->config;
	uint8_t response;

	spi_write_dt(&config->bus, &data, 1, &response, 1);
	return response;
}

/**
 * @brief Send SD command and get R1 response
 * @param dev SD card device
 * @param cmd Command code
 * @param arg Command argument
 * @param crc CRC for command (only first byte used)
 * @return R1 response byte
 */
static uint8_t sd_spi_send_cmd(const struct device *dev,
			      uint8_t cmd,
			      uint32_t arg,
			      uint8_t crc)
{
	uint8_t response;
	uint32_t retries = SD_BUSY_RETRY_COUNT;

	sd_spi_deselect(dev);
	sd_spi_select(dev);

	/* Send command packet (6 bytes) */
	sd_spi_xfer_byte(dev, cmd | 0x40);  /* Start bit + cmd index */
	sd_spi_xfer_byte(dev, (arg >> 24) & 0xFF);
	sd_spi_xfer_byte(dev, (arg >> 16) & 0xFF);
	sd_spi_xfer_byte(dev, (arg >> 8) & 0xFF);
	sd_spi_xfer_byte(dev, arg & 0xFF);
	sd_spi_xfer_byte(dev, crc);

	/* Skip stuff byte for CMD12 */
	if (cmd == CMD12) {
		sd_spi_xfer_byte(dev, 0xFF);
	}

	/* Wait for response (byte with bit 7 = 0) */
	do {
		response = sd_spi_xfer_byte(dev, 0xFF);
	} while ((response & 0x80) && retries--);

	return response;
}

/**
 * @brief Receive data packet from SD card
 * @param dev SD card device
 * @param buf Buffer to receive data
 * @param len Number of bytes to receive
 * @return 0 on success, error code otherwise
 */
static int sd_spi_recv_data(const struct device *dev, uint8_t *buf, uint16_t len)
{
	uint16_t i;
	uint8_t token;

	/* Wait for data start token (0xFE) */
	for (i = 0; i < SD_BUSY_RETRY_COUNT; i++) {
		token = sd_spi_xfer_byte(dev, 0xFF);
		if (token == SD_START_BLOCK) {
			break;
		}
	}

	if (token != SD_START_BLOCK) {
		LOG_ERR("No data start token: 0x%02X", token);
		return -EIO;
	}

	/* Read data bytes */
	for (i = 0; i < len; i++) {
		buf[i] = sd_spi_xfer_byte(dev, 0xFF);
	}

	/* Read 16-bit CRC (discard for our implementation) */
	sd_spi_xfer_byte(dev, 0xFF);
	sd_spi_xfer_byte(dev, 0xFF);

	return 0;
}

/**
 * @brief Send data block to SD card
 * @param dev SD card device
 * @param buf Buffer containing data
 * @param cmd Data start token
 * @return 0 on success, error code otherwise
 */
static int sd_spi_send_block(const struct device *dev,
			   const uint8_t *buf,
			   uint8_t cmd)
{
	uint16_t i;
	uint8_t response;

	if (sd_spi_wait_ready(dev)) {
		return -EBUSY;
	}

	sd_spi_xfer_byte(dev, cmd);

	if (cmd != SD_STOP_TRAN) {
		/* Send data bytes */
		for (i = 0; i < SD_BLOCK_SIZE; i++) {
			sd_spi_xfer_byte(dev, buf[i]);
		}

		/* Send 16-bit CRC */
		sd_spi_xfer_byte(dev, 0xFF);
		sd_spi_xfer_byte(dev, 0xFF);

		/* Read data response token */
		response = sd_spi_xfer_byte(dev, 0xFF);
		if ((response & DATA_TOKEN_MASK) != DATA_TOKEN_ACCEPTED) {
			LOG_ERR("Data response error: 0x%02X", response);
			return -EIO;
		}
	}

	return 0;
}

/* ============================================================================
 * SD Card Information Functions
 * ============================================================================ */

/**
 * @brief Read CID (Card Identification) register
 */
static int sd_spi_read_cid(const struct device *dev, uint8_t *cid_data)
{
	uint8_t r1;

	r1 = sd_spi_send_cmd(dev, CMD10, 0, 0x01);
	if (r1 == 0x00) {
		r1 = sd_spi_recv_data(dev, cid_data, 16);
	}
	sd_spi_deselect(dev);
	return r1;
}

/**
 * @brief Read CSD (Card Specific Data) register
 */
static int sd_spi_read_csd(const struct device *dev, uint8_t *csd_data)
{
	uint8_t r1;

	r1 = sd_spi_send_cmd(dev, CMD9, 0, 0x01);
	if (r1 == 0x00) {
		r1 = sd_spi_recv_data(dev, csd_data, 16);
	}
	sd_spi_deselect(dev);
	return r1;
}

/**
 * @brief Calculate SD card capacity from CSD
 */
static uint32_t sd_spi_get_capacity(const struct device *dev)
{
	const struct sd_spi_data *data = dev->data;
	uint8_t csd[16];
	uint32_t capacity;
	uint8_t csize_mult;
	uint16_t csize;
	uint8_t read_bl_len;

	if (sd_spi_read_csd(dev, csd) != 0) {
		return 0;
	}

	/* CSD structure version */
	if ((csd[0] & 0xC0) == 0x40) {
		/* CSD version 2.0 (SDHC/SDXC) */
		csize = (csd[9] << 8) | csd[8];
		capacity = (uint32_t)(csize + 1) * 512 * 1024;  /* in bytes */
		data->card_type = SD_TYPE_V2HC;
	} else {
		/* CSD version 1.0 (SDSC) */
		csize = ((csd[6] & 0x03) << 10) |
			(csd[7] << 2) |
			((csd[8] & 0xC0) >> 6);
		csize_mult = ((csd[9] & 0x03) << 1) |
			     ((csd[10] & 0x80) >> 7);
		read_bl_len = csd[5] & 0x0F;
		capacity = (csize + 1) *
			   (1 << (csize_mult + 2)) *
			   (1 << read_bl_len);
		data->card_type = SD_TYPE_V2;
	}

	data->sector_count = capacity / SD_BLOCK_SIZE;

	LOG_INF("Card capacity: %u sectors (%u MB)",
		data->sector_count,
		(uint32_t)(capacity / (1024 * 1024)));

	return capacity / SD_BLOCK_SIZE;
}

/* ============================================================================
 * SD Card Initialization
 * ============================================================================ */

/**
 * @brief Initialize SD card
 */
static int sd_spi_card_init(const struct device *dev)
{
	const struct sd_spi_config *config = dev->config;
	struct sd_spi_data *data = dev->data;
	uint8_t r1;
	uint8_t cmd8_response[4];
	uint32_t retry;
	uint32_t i;
	uint8_t ocr[4];

	LOG_INF("Initializing SD card...");

	/* Configure SPI for initialization (low speed) */
	data->spi_cfg.frequency = config->init_clk_freq;
	data->spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
				 SPI_MODE_CPOL | SPI_MODE_CPHA;
	spi_configure(data->dev, &data->spi_cfg);

	/* Send 80 clock cycles to power up the card */
	for (i = 0; i < 10; i++) {
		sd_spi_xfer_byte(dev, 0xFF);
	}

	/* Send CMD0 to reset card to idle state */
	retry = 20;
	do {
		r1 = sd_spi_send_cmd(dev, CMD0, 0, 0x95);
	} while ((r1 != R1_IN_IDLE_STATE) && retry--);

	if (r1 != R1_IN_IDLE_STATE) {
		LOG_ERR("CMD0 failed: 0x%02X", r1);
		return -EIO;
	}

	data->card_type = SD_TYPE_ERR;

	/* Check for SD 2.0 card using CMD8 */
	if (sd_spi_send_cmd(dev, CMD8, 0x1AA, 0x87) == R1_ILLEGAL_CMD) {
		/* SD 1.x or MMC */
		LOG_INF("Detected SD 1.x or MMC card");
		data->card_type = SD_TYPE_V1;

		/* Try to initialize as SD card */
		retry = 0xFFFF;
		do {
			sd_spi_send_cmd(dev, CMD55, 0, 0x01);
			r1 = sd_spi_send_cmd(dev, CMD41, 0x00, 0x01);
		} while (r1 && retry--);

		if (retry && sd_spi_send_cmd(dev, CMD16, SD_BLOCK_SIZE, 0x01) == 0) {
			LOG_INF("SD 1.x initialized");
		} else {
			LOG_ERR("Failed to initialize SD 1.x card");
			return -EIO;
		}
	} else {
		/* SD 2.0 card */
		LOG_INF("Detected SD 2.0 card");

		/* Read CMD8 response */
		for (i = 0; i < 4; i++) {
			cmd8_response[i] = sd_spi_xfer_byte(dev, 0xFF);
		}

		if (cmd8_response[2] == 0x01 && cmd8_response[3] == 0xAA) {
			LOG_INF("Card supports 2.7-3.6V");

			/* Initialize SD card with ACMD41 */
			retry = 0xFFFF;
			do {
				sd_spi_send_cmd(dev, CMD55, 0, 0x01);
				r1 = sd_spi_send_cmd(dev, CMD41, 0x40000000, 0x01);
			} while (r1 && retry--);

			if (retry && sd_spi_send_cmd(dev, CMD58, 0, 0x01) == 0) {
				/* Read OCR to check for SDHC */
				for (i = 0; i < 4; i++) {
					ocr[i] = sd_spi_xfer_byte(dev, 0xFF);
				}

				if (ocr[0] & 0x40) {
					data->card_type = SD_TYPE_V2HC;
					LOG_INF("SDHC card detected");
				} else {
					data->card_type = SD_TYPE_V2;
					LOG_INF("SDSC card detected");
				}
			} else {
				LOG_ERR("SD 2.0 initialization failed");
				return -EIO;
			}
		}
	}

	sd_spi_deselect(dev);

	/* Switch to high speed */
	data->spi_cfg.frequency = config->max_clk_freq;
	spi_configure(data->dev, &data->spi_cfg);

	/* Get card capacity */
	if (sd_spi_get_capacity(dev) == 0) {
		LOG_ERR("Failed to get card capacity");
		return -EIO;
	}

	data->initialized = true;
	LOG_INF("SD card initialized: %u sectors", data->sector_count);

	return 0;
}

/* ============================================================================
 * Public API - Read/Write Operations
 * ============================================================================ */

/**
 * @brief Read single block from SD card
 */
static inline int sd_spi_read_block(const struct device *dev,
				  uint32_t sector,
				  uint8_t *data)
{
	const struct sd_spi_data *drv_data = dev->data;
	uint8_t r1;
	int ret;

	/* Convert sector address for non-SDHC cards */
	uint32_t addr = (drv_data->card_type == SD_TYPE_V2HC) ?
			 sector : sector * SD_BLOCK_SIZE;

	k_mutex_lock(&drv_data->lock, K_FOREVER);

	r1 = sd_spi_send_cmd(dev, CMD17, addr, 0x01);
	if (r1 == 0) {
		ret = sd_spi_recv_data(dev, data, SD_BLOCK_SIZE);
	} else {
		LOG_ERR("CMD17 failed: 0x%02X", r1);
		ret = -EIO;
	}

	sd_spi_deselect(dev);
	k_mutex_unlock(&drv_data->lock);

	return ret;
}

/**
 * @brief Write single block to SD card
 */
static inline int sd_spi_write_block(const struct device *dev,
				   uint32_t sector,
				   const uint8_t *data)
{
	const struct sd_spi_data *drv_data = dev->data;
	uint8_t r1;
	int ret;

	/* Check write protect */
	if (drv_data->write_protected) {
		LOG_WRN("Card is write protected");
		return -EACCES;
	}

	/* Convert sector address for non-SDHC cards */
	uint32_t addr = (drv_data->card_type == SD_TYPE_V2HC) ?
			 sector : sector * SD_BLOCK_SIZE;

	k_mutex_lock(&drv_data->lock, K_FOREVER);

	r1 = sd_spi_send_cmd(dev, CMD24, addr, 0x01);
	if (r1 == 0) {
		ret = sd_spi_send_block(dev, data, SD_START_BLOCK);
	} else {
		LOG_ERR("CMD24 failed: 0x%02X", r1);
		ret = -EIO;
	}

	sd_spi_deselect(dev);
	k_mutex_unlock(&drv_data->lock);

	return ret;
}

/**
 * @brief Read multiple blocks from SD card
 */
static inline int sd_spi_read_blocks(const struct device *dev,
				   uint32_t sector,
				   uint8_t *data,
				   uint32_t count)
{
	const struct sd_spi_data *drv_data = dev->data;
	uint8_t r1;
	uint32_t i;
	int ret = 0;

	/* Convert sector address for non-SDHC cards */
	uint32_t addr = (drv_data->card_type == SD_TYPE_V2HC) ?
			 sector : sector * SD_BLOCK_SIZE;

	k_mutex_lock(&drv_data->lock, K_FOREVER);

	r1 = sd_spi_send_cmd(dev, CMD18, addr, 0x01);
	if (r1 == 0) {
		for (i = 0; i < count && ret == 0; i++) {
			ret = sd_spi_recv_data(dev, data, SD_BLOCK_SIZE);
			data += SD_BLOCK_SIZE;
		}
		sd_spi_send_cmd(dev, CMD12, 0, 0x01);  /* Stop transmission */
	} else {
		LOG_ERR("CMD18 failed: 0x%02X", r1);
		ret = -EIO;
	}

	sd_spi_deselect(dev);
	k_mutex_unlock(&drv_data->lock);

	return ret;
}

/**
 * @brief Write multiple blocks to SD card
 */
static inline int sd_spi_write_blocks(const struct device *dev,
				    uint32_t sector,
				    const uint8_t *data,
				    uint32_t count)
{
	const struct sd_spi_data *drv_data = dev->data;
	uint8_t r1;
	uint32_t i;
	int ret = 0;

	/* Check write protect */
	if (drv_data->write_protected) {
		LOG_WRN("Card is write protected");
		return -EACCES;
	}

	/* Convert sector address for non-SDHC cards */
	uint32_t addr = (drv_data->card_type == SD_TYPE_V2HC) ?
			 sector : sector * SD_BLOCK_SIZE;

	k_mutex_lock(&drv_data->lock, K_FOREVER);

	/* Pre-erase blocks for better performance */
	if (drv_data->card_type != SD_TYPE_MMC) {
		sd_spi_send_cmd(dev, CMD55, 0, 0x01);
		sd_spi_send_cmd(dev, CMD23, count, 0x01);
	}

	r1 = sd_spi_send_cmd(dev, CMD25, addr, 0x01);
	if (r1 == 0) {
		for (i = 0; i < count && ret == 0; i++) {
			ret = sd_spi_send_block(dev, data,
					(i == count - 1) ?
					SD_STOP_TRAN : SD_START_BLOCK_MULT);
			data += SD_BLOCK_SIZE;
		}
		sd_spi_send_block(dev, NULL, SD_STOP_TRAN);  /* Stop token */
	} else {
		LOG_ERR("CMD25 failed: 0x%02X", r1);
		ret = -EIO;
	}

	sd_spi_deselect(dev);
	k_mutex_unlock(&drv_data->lock);

	return ret;
}

/* ============================================================================
 * Disk Access Subsystem Integration
 * ============================================================================ */

#ifdef CONFIG_DISK_ACCESS

static int sd_spi_disk_init(const struct device *dev)
{
	struct sd_spi_data *data = dev->data;

	LOG_DBG("Disk init: %s", dev->name);

	if (!data->initialized) {
		return -EIO;
	}

	/* Initialize disk_info */
	data->disk_info.sector_size = SD_BLOCK_SIZE;
	data->disk_info.sector_count = data->sector_count;
	data->disk_info.flags = 0;

	LOG_INF("Disk initialized: %u sectors, %u bytes/sector",
		data->disk_info.sector_count,
		data->disk_info.sector_size);

	return 0;
}

static int sd_spi_disk_read(const struct device *dev,
			  uint8_t *data_buf,
			  uint32_t start_sector,
			  uint32_t num_sector)
{
	int ret;

	LOG_DBG("Disk read: sector=%u, count=%u", start_sector, num_sector);

	if (num_sector == 1) {
		ret = sd_spi_read_block(dev, start_sector, data_buf);
	} else {
		ret = sd_spi_read_blocks(dev, start_sector, data_buf, num_sector);
	}

	return ret;
}

static int sd_spi_disk_write(const struct device *dev,
			   const uint8_t *data_buf,
			   uint32_t start_sector,
			   uint32_t num_sector)
{
	int ret;

	LOG_DBG("Disk write: sector=%u, count=%u", start_sector, num_sector);

	if (num_sector == 1) {
		ret = sd_spi_write_block(dev, start_sector, data_buf);
	} else {
		ret = sd_spi_write_blocks(dev, start_sector, data_buf, num_sector);
	}

	return ret;
}

static int sd_spi_disk_ioctl(const struct device *dev,
			   uint8_t cmd,
			   void *buf)
{
	struct sd_spi_data *data = dev->data;

	switch (cmd) {
	case DISK_IOCTL_GET_SECTOR_COUNT:
		*(uint32_t *)buf = data->sector_count;
		return 0;

	case DISK_IOCTL_GET_SECTOR_SIZE:
		*(uint32_t *)buf = SD_BLOCK_SIZE;
		return 0;

	case DISK_IOCTL_GET_ERASE_BLOCK_SIZE:
		*(uint32_t *)buf = SD_BLOCK_SIZE;
		return 0;

	case DISK_IOCTL_CTRL_SYNC:
		LOG_DBG("Disk sync");
		return 0;

	default:
		return -ENOTSUP;
	}
}

static const struct disk_operations sd_spi_disk_ops = {
	.init = sd_spi_disk_init,
	.status = NULL,
	.read = sd_spi_disk_read,
	.write = sd_spi_disk_write,
	.ioctl = sd_spi_disk_ioctl,
};

#endif /* CONFIG_DISK_ACCESS */

/* ============================================================================
 * Device Driver Initialization
 * ============================================================================ */

static int sd_spi_init(const struct device *dev)
{
	const struct sd_spi_config *config = dev->config;
	struct sd_spi_data *data = dev->data;
	int ret;

	LOG_INF("Initializing custom SD SPI driver...");

	/* Initialize mutex */
	k_mutex_init(&data->lock);
	k_sem_init(&data->card_sem, 0, 1);

	/* Configure SPI device */
	data->dev = (const struct device *)spi_get_dt_spec(&config->bus).bus;

	/* Configure CS pin */
	if (config->cs.port) {
		if (!gpio_is_ready_dt(&config->cs)) {
			LOG_ERR("CS GPIO not ready");
			return -ENODEV;
		}
		gpio_pin_configure_dt(&config->cs, GPIO_OUTPUT_HIGH);
	}

	/* Configure card detect pin */
	if (config->cd.port) {
		if (!gpio_is_ready_dt(&config->cd)) {
			LOG_WRN("CD GPIO not ready");
		} else {
			gpio_pin_configure_dt(&config->cd, GPIO_INPUT);
			gpio_pin_interrupt_configure_dt(&config->cd,
						      GPIO_INT_EDGE_BOTH,
						      NULL);
			data->present = !gpio_pin_get_dt(&config->cd);
			LOG_INF("Card detect: present=%d", data->present);
		}
	} else {
		data->present = true;  /* Assume present if no CD pin */
	}

	/* Configure write protect pin */
	if (config->wp.port) {
		if (!gpio_is_ready_dt(&config->wp)) {
			LOG_WRN("WP GPIO not ready");
		} else {
			gpio_pin_configure_dt(&config->wp, GPIO_INPUT);
			data->write_protected = gpio_pin_get_dt(&config->wp);
		}
	}

	/* Configure power control pin */
	if (config->power.port) {
		if (!gpio_is_ready_dt(&config->power)) {
			LOG_WRN("Power GPIO not ready");
		} else {
			gpio_pin_configure_dt(&config->power, GPIO_OUTPUT_HIGH);
			k_msleep(10);  /* Power-up delay */
		}
	}

	/* Initialize SD card */
	if (data->present) {
		ret = sd_spi_card_init(dev);
		if (ret != 0) {
			LOG_ERR("SD card initialization failed: %d", ret);
			return ret;
		}
	} else {
		LOG_WRN("No SD card detected");
		return -ENODEV;
	}

	LOG_INF("Custom SD SPI driver initialized");
	return 0;
}

/* Device instantiation macro */
#define CUSTOM_SD_SPI_SDMMC_DEFINE(inst)					     \
	static struct sd_spi_data sd_spi_data_##inst;			     \
	static const struct sd_spi_config sd_spi_config_##inst = {		     \
		.bus = SPI_DT_SPEC_INST_GET(inst),			     \
		.cs = GPIO_DT_SPEC_INST_GET_OR(inst, cs_gpios, {0}),	     \
		.cd = GPIO_DT_SPEC_INST_GET_OR(inst, cd_gpios, {0}),	     \
		.wp = GPIO_DT_SPEC_INST_GET_OR(inst, wp_gpios, {0}),	     \
		.power = GPIO_DT_SPEC_INST_GET_OR(inst, power_gpios, {0}),     \
		.max_clk_freq = DT_INST_PROP(inst, spi_max_frequency),	     \
		.init_clk_freq = DT_INST_PROP_OR(inst,			       \
					spi_init_frequency,		     \
					CONFIG_CUSTOM_SD_SPI_SDMMC_SPI_CLK_FREQ_INIT), \
		.use_dma = DT_INST_PROP_OR(inst, use_dma,			     \
					CONFIG_CUSTOM_SD_SPI_SDMMC_USE_DMA), \
	};								     \
									     \
	DEVICE_DT_INST_DEFINE(inst,					     \
			  sd_spi_init,				     \
			  NULL,					     \
			  &sd_spi_data_##inst,			     \
			  &sd_spi_config_##inst,			     \
			  POST_KERNEL,				     \
			  CONFIG_KERNEL_INIT_PRIORITY_DEVICE,		     \
			  &sd_spi_disk_ops);			     \

/* Instantiate all enabled devices */
DT_INST_FOREACH_STATUS_OKAY(CUSTOM_SD_SPI_SDMMC_DEFINE)
