/*
 * Copyright (c) 2024, Custom Driver Module
 * SPDX-License-Identifier: Apache-2.0
 *
 * Custom SD SPI SDMMC Driver
 * Based on SD 2.0 specification
 */

#ifndef ZEPHYR_DRIVERS_STORAGE_CUSTOM_SD_SPI_SDMMC_H_
#define ZEPHYR_DRIVERS_STORAGE_CUSTOM_SD_SPI_SDMMC_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/util.h>

/* SD Card Types */
#define SD_TYPE_ERR        0x00
#define SD_TYPE_MMC        0x01
#define SD_TYPE_V1         0x02
#define SD_TYPE_V2         0x04
#define SD_TYPE_V2HC       0x06

/* SD Card Commands */
#define CMD0    0       /* GO_IDLE_STATE - Reset card to idle state */
#define CMD1    1       /* SEND_OP_COND - Initialize card */
#define CMD8    8       /* SEND_IF_COND - Check voltage range and version */
#define CMD9    9       /* SEND_CSD - Read CSD register */
#define CMD10   10      /* SEND_CID - Read CID register */
#define CMD12   12      /* STOP_TRANSMISSION - Stop data transmission */
#define CMD16   16      /* SET_BLOCKLEN - Set block size (should be 512) */
#define CMD17   17      /* READ_SINGLE_BLOCK - Read one block */
#define CMD18   18      /* READ_MULTIPLE_BLOCK - Read multiple blocks */
#define CMD23   23      /* SET_BLOCK_COUNT - Set number of blocks before write */
#define CMD24   24      /* WRITE_SINGLE_BLOCK - Write one block */
#define CMD25   25      /* WRITE_MULTIPLE_BLOCK - Write multiple blocks */
#define CMD41   41      /* SD_SEND_OP_COND - Initialize SD card (after CMD55) */
#define CMD55   55      /* APP_CMD - Next command is application command */
#define CMD58   58      /* READ_OCR - Read OCR register */
#define CMD59   59      /* CRC_ON_OFF - Enable/disable CRC */

/* SD Card Response Types */
#define R1_NO_ERROR          0x00
#define R1_IN_IDLE_STATE     0x01
#define R1_ILLEGAL_CMD       0x04
#define R1_CRC_ERROR         0x08
#define R1_ERASE_SEQ_ERROR   0x10
#define R1_ADDR_ERROR        0x20
#define R1_PARAM_ERROR       0x40

/* Data Tokens */
#define SD_START_BLOCK     0xFE  /* Data start token for read */
#define SD_START_BLOCK_MULT 0xFC  /* Data start token for multi-write */
#define SD_STOP_TRAN      0xFD  /* Stop token for multi-write */

/* Data Response Token */
#define DATA_TOKEN_MASK         0x1F
#define DATA_TOKEN_ACCEPTED     0x05
#define DATA_TOKEN_CRC_ERR      0x0B
#define DATA_TOKEN_WRITE_ERR    0x0D
#define DATA_TOKEN_OTHER_ERR    0xFF

/* SD Card Block Size */
#define SD_BLOCK_SIZE  512

/* Timing Constants */
#define SD_INIT_CLOCK_FREQ      400000   /* 400kHz for initialization */
#define SD_WRITE_TIMEOUT_MS     500
#define SD_READ_TIMEOUT_MS      1000
#define SD_CMD_TIMEOUT_MS      100
#define SD_BUSY_RETRY_COUNT    10000

/* SD Card Capacity Information */
struct sd_card_info {
	uint32_t sector_count;  /* Total number of sectors */
	uint32_t block_size;     /* Block size (typically 512) */
	uint8_t  card_type;     /* SD card type */
	uint8_t  version;       /* SD card version */
	char     oem[7];       /* OEM/Application ID */
	char     product[6];    /* Product name */
	uint32_t serial;        /* Serial number */
	uint16_t manufacturing_year;
	uint16_t manufacturing_month;
};

/* SD Card Configuration */
struct sd_spi_config {
	struct spi_dt_spec bus;        /* SPI bus specification */
	struct gpio_dt_spec cs;        /* Chip select GPIO */
	struct gpio_dt_spec cd;        /* Card detect GPIO */
	struct gpio_dt_spec wp;        /* Write protect GPIO */
	struct gpio_dt_spec power;     /* Power control GPIO */
	uint32_t max_clk_freq;        /* Maximum SPI clock frequency */
	uint32_t init_clk_freq;       /* Initialization clock frequency */
	bool use_dma;                 /* Use DMA for transfers */
};

/* SD Card Driver Data */
struct sd_spi_data {
	const struct device *dev;
	struct spi_config spi_cfg;
	struct k_mutex lock;          /* Mutex for thread safety */
	struct k_sem card_sem;        /* Semaphore for card presence */

	uint8_t  card_type;         /* Detected card type */
	uint32_t sector_count;      /* Total sectors */
	bool      initialized;       /* Initialization flag */
	bool      present;           /* Card present flag */
	bool      write_protected;   /* Write protect status */

#ifdef CONFIG_DISK_ACCESS
	struct disk_info disk_info;   /* Disk information for disk_access */
#endif

#if CONFIG_CUSTOM_SD_SPI_SDMMC_USE_DMA
	uint8_t  tx_dma_buf[SD_BLOCK_SIZE + 16]; /* TX buffer with padding */
	uint8_t  rx_dma_buf[SD_BLOCK_SIZE + 16]; /* RX buffer with padding */
	struct k_work dma_work;      /* DMA completion work */
	struct k_event dma_events;   /* DMA event flags */
#endif
};

/* Driver API */
static inline int sd_spi_read_block(const struct device *dev,
				  uint32_t sector,
				  uint8_t *data);

static inline int sd_spi_write_block(const struct device *dev,
				   uint32_t sector,
				   const uint8_t *data);

static inline int sd_spi_read_blocks(const struct device *dev,
				   uint32_t sector,
				   uint8_t *data,
				   uint32_t count);

static inline int sd_spi_write_blocks(const struct device *dev,
				    uint32_t sector,
				    const uint8_t *data,
				    uint32_t count);

/* Card Detection Callback */
typedef void (*sd_card_callback_t)(const struct device *dev, bool inserted);

int sd_spi_register_callback(const struct device *dev, sd_card_callback_t cb);

#endif /* ZEPHYR_DRIVERS_STORAGE_CUSTOM_SD_SPI_SDMMC_H_ */
