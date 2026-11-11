# SD card (SPI mode) + FatFs on FRDM-RW612, FreeRTOS build.
#
# The SD driver, the FatFs disk glue and the test sequence are NOT copied: they are compiled straight
# from the bare-metal project next to this one. Only the entry point (tasks), ffconf.h and the Kconfig
# selection differ.

cmake_minimum_required(VERSION 3.22.0)

cmake_path(APPEND conf_file_path ${CMAKE_CURRENT_LIST_DIR} ${board} prj.conf)
set(CONF_FILE ${conf_file_path} ${CONF_FILE})
include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(frdmrw612_sdspi_fatfs_freertos LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH ${board_root}/${board}/demo_apps/hello_world)

include(${SdkRootDirPath}/CMakeLists.txt)


include(${board}/board_files.cmake)

# Shared sources live in the bare-metal project.
cmake_path(SET SHARED_SRC NORMALIZE "${CMAKE_CURRENT_LIST_DIR}/../frdmrw612_sdspi_fatfs/source")
if(NOT EXISTS "${SHARED_SRC}/sdspi/sd_spi.c")
    message(FATAL_ERROR "Shared sources not found at ${SHARED_SRC}. Keep frdmrw612_sdspi_fatfs next to this project.")
endif()

mcux_add_source(
    SOURCES source/main_freertos.c
            source/fatfs_config/ffconf.h
)

mcux_add_source(
    BASE_PATH ${SHARED_SRC}
    SOURCES sd_test.c
            sd_test.h
            sdspi/sd_spi_port.c
            sdspi/sd_spi_port.h
            sdspi/sd_spi.c
            sdspi/sd_spi.h
            fatfs/sdspi_disk.c
            fatfs/fsl_sdspi_disk.h
)

mcux_add_include(
    BASE_PATH ${SHARED_SRC}
    INCLUDES .
             sdspi
             fatfs
)

# This project's own ffconf.h (FF_FS_REENTRANT = 1); the bare-metal one is not on the include path.
mcux_add_include(INCLUDES . source source/fatfs_config)

# Stack of main() before the scheduler starts; tasks have their own stacks.
mcux_add_armgcc_configuration(
    LD "-Xlinker --defsym=__stack_size__=0x1000"
)

mcux_add_configuration(
    # Zero-padded / width formats (%02X, %08X, %10u) in PRINTF need the advanced formatter.
    CC "-DPRINTF_ADVANCED_ENABLE=1 -DSD_SPI_PORT_FREERTOS=1 -DSD_SPI_PORT_DMA=1"
)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
mcux_add_config_mex_path( PATH ${board} )
