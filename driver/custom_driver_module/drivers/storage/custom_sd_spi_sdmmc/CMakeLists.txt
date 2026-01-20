# SPDX-License-Identifier: Apache-2.0

zephyr_sources_ifdef(CONFIG_CUSTOM_SD_SPI_SDMMC
	drivers/storage/custom_sd_spi_sdmmc.c
)
