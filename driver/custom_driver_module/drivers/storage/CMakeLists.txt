# SPDX-License-Identifier: Apache-2.0

if(CONFIG_CUSTOM_SD_SPI_SDMMC)

zephyr_library_named(custom_sd_spi_sdmmc)

target_sources(zephyr_interface PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/custom_sd_spi_sdmmc.c
)

target_include_directories(zephyr_interface PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

zephyr_library_include_directories(ZEPHYR_BASE)

endif()
