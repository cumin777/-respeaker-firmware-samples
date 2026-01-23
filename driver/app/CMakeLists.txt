#-------------------------------------------------------------------------------
# Zephyr Example Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)

# Work around Windows path-length limits.
# This project pulls in an external module whose generated object/dependency
# file paths can exceed the safe limit on Windows, causing GCC to fail writing
# the .obj.d depfile.
if(WIN32)
  # Use a very small value to force hashing/shortening of object paths.
  # This avoids GCC failing to create depfiles (.obj.d) under deep module paths.
  set(CMAKE_OBJECT_PATH_MAX 60)
endif()

# STEP 8.1 Add external module
list(APPEND EXTRA_ZEPHYR_MODULES 
  ${CMAKE_CURRENT_SOURCE_DIR}/../custom_driver_module 
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

if(CONFIG_RESPEAKER_APP_VARIANT_SIMPLE_UI)
  target_sources(app PRIVATE
    simple_ui.c
    src/button.c
  )
elseif(CONFIG_RESPEAKER_APP_VARIANT_RTC_DEMO)
  target_sources(app PRIVATE
    rtc_demo.c
  )
else()
  target_sources(app PRIVATE
    src/main.c
    src/button.c
  )
endif()