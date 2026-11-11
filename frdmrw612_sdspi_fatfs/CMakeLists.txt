
# Copyright 2026 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.22.0)

cmake_path(APPEND conf_file_path ${CMAKE_CURRENT_LIST_DIR} ${board} prj.conf)
set(CONF_FILE ${conf_file_path} ${CONF_FILE})
include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(frdmrw612_sdspi_fatfs LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH ${board_root}/${board}/demo_apps/hello_world)

include(${SdkRootDirPath}/CMakeLists.txt)


include(${board}/board_files.cmake)

mcux_add_source(
    SOURCES source/main.c
            source/sd_test.c
            source/sd_test.h
            source/sdspi/sd_spi_port.c
            source/sdspi/sd_spi_port.h
            source/sdspi/sd_spi.c
            source/sdspi/sd_spi.h
            source/fatfs/sdspi_disk.c
            source/fatfs/fsl_sdspi_disk.h
            source/fatfs_config/ffconf.h
)

# FatFs (LFN on stack) needs more than the 1 KB default stack of the RW612 linker scripts.
mcux_add_armgcc_configuration(
    LD "-Xlinker --defsym=__stack_size__=0x2000"
)

# Zero-padded / width formats (%02X, %08X, %10u) in PRINTF need the advanced formatter.
mcux_add_configuration(
    CC "-DPRINTF_ADVANCED_ENABLE=1"
)

# Build SPI DMA support (fsl_spi_dma). The test then runs the throughput pass once per transfer mode.
mcux_add_configuration(
    CC "-DSD_SPI_PORT_DMA=1"
)

# Read-path timing breakdown (diagnostic): uncomment to print a "Read profile" block after the throughput test.
# mcux_add_configuration(
#     CC "-DSD_SPI_PROFILE=1"
# )

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
mcux_add_config_mex_path( PATH ${board} )
mcux_add_include(INCLUDES . source source/sdspi source/fatfs source/fatfs_config)
