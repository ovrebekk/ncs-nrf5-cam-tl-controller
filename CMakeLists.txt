#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

set(PM_STATIC_YML_FILE
  ${CMAKE_CURRENT_SOURCE_DIR}/boards/pm_static_${BOARD}.yml
  )

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

target_include_directories(app PRIVATE include)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/cam_tl_control.c
  src/flash_handler.c
)

# NORDIC SDK APP END
zephyr_library_include_directories(.)
