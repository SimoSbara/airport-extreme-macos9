# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
# AirPort Extreme for Mac OS 9 - build with Retro68 (GCC, classic PowerPC)
#
# In the Debian VM (see Build/build.sh):
#   cmake -S Build -B out -DCMAKE_TOOLCHAIN_FILE=<Retro68-build>/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
#   cmake --build out
#
# The Linux and wpa_supplicant code is the one in Common/, without
# modifications: with GCC the GNU extensions work as they do in Linux.

cmake_minimum_required(VERSION 3.15)
project(AirPortExtreme C CXX)

set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../Common)
set(TESTER ${CMAKE_CURRENT_SOURCE_DIR}/../AirPortExtremeTester)
set(TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/../Tools)

# Broadcom firmware (not redistributable, kept out of the repository): the .fw
# files extracted with b43-fwcutter go in firmware/ (see firmware/README.md).
# b43fw.c is generated here, in the build directory.
set(FIRMWARE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../firmware CACHE PATH "Directory with the b43 .fw files")
set(FIRMWARE_FILES
    ${FIRMWARE_DIR}/ucode5.fw ${FIRMWARE_DIR}/pcm5.fw
    ${FIRMWARE_DIR}/b0g0initvals5.fw ${FIRMWARE_DIR}/b0g0bsinitvals5.fw)
foreach(f ${FIRMWARE_FILES})
    if(NOT EXISTS ${f})
        message(FATAL_ERROR "Missing firmware ${f}: see firmware/README.md")
    endif()
endforeach()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/b43fw.c
    COMMAND ${Python3_EXECUTABLE} ${TOOLS}/fw2c.py ${FIRMWARE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/b43fw.c
    DEPENDS ${FIRMWARE_FILES} ${TOOLS}/fw2c.py)

# Options as in the Linux kernel (top-level Makefile)
set(LINUX_CFLAGS
    -std=gnu11
    -Os
    -fno-strict-aliasing
    -fno-strict-overflow
    -fno-delete-null-pointer-checks
    -fno-common
    -ffunction-sections
    -mtraceback=full            # function names in MacsBug
    -Wall
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-address-of-packed-member
    -Wno-pointer-sign
    -Wno-missing-braces
)

# ---------------------------------------------------------------------------
# Linux 5.15 wireless stack: b43, ssb, mac80211, cfg80211 + compat + os9
# ---------------------------------------------------------------------------
file(GLOB LINUX_SOURCES
    ${COMMON}/b43/*.c
    ${COMMON}/ssb/*.c
    ${COMMON}/mac80211/*.c
    ${COMMON}/cfg80211/*.c
    ${COMMON}/lib/*.c
    ${COMMON}/compat/*.c
    ${COMMON}/os9/*.c)
# The EXTIF driver is only needed by CodeWarrior (see b43compat.h)
list(REMOVE_ITEM LINUX_SOURCES ${COMMON}/ssb/ssb_driver_extif.c)
list(APPEND LINUX_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/b43fw.c)

add_library(linux80211 STATIC ${LINUX_SOURCES})
target_include_directories(linux80211 PRIVATE
    ${COMMON}/compat
    ${COMMON}/include
    ${COMMON}/os9
    ${COMMON}/cfg80211          # b43os9.c uses core.h and rdev-ops.h
    ${COMMON}/b43)              # b43os9_hw.c uses b43.h
target_compile_options(linux80211 PRIVATE ${LINUX_CFLAGS})

# ---------------------------------------------------------------------------
# wpa_supplicant 2.11 (RSN supplicant) + Mac OS 9 port
# ---------------------------------------------------------------------------
file(GLOB_RECURSE WPA_SOURCES ${COMMON}/wpa/src/*.c)
list(APPEND WPA_SOURCES
    ${COMMON}/wpa/os9/os_os9.c
    ${COMMON}/wpa/os9/eloop_os9.c
    ${COMMON}/wpa/os9/wpas_os9.c)

add_library(wpas STATIC ${WPA_SOURCES})
target_include_directories(wpas PRIVATE
    ${COMMON}/wpa/os9           # build_config.h for Mac OS 9 (before everything else)
    ${COMMON}/wpa/src
    ${COMMON}/wpa/src/utils
    ${COMMON}/os9)
# POSIX headers missing in Retro68: used only if the C library lacks them
target_compile_options(wpas PRIVATE
    -std=gnu11 -Os -ffunction-sections -mtraceback=full -Wall -Wno-unused-function
    -idirafter ${COMMON}/wpa/os9/posix)
target_link_libraries(wpas PUBLIC linux80211)

# ---------------------------------------------------------------------------
# Tester (console application)
# ---------------------------------------------------------------------------
add_application(AirPortTester
    ${TESTER}/AirPortTester.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../AirPortExtremeDriver/drv_pool.c
    ${CMAKE_CURRENT_SOURCE_DIR}/AirPortTester.r
    CONSOLE)
target_include_directories(AirPortTester PRIVATE ${TESTER} ${COMMON}/os9
    ${CMAKE_CURRENT_SOURCE_DIR}/../AirPortExtremeDriver)
target_compile_options(AirPortTester PRIVATE -std=gnu11 -Os -ffunction-sections -mtraceback=full -Wall)
target_link_libraries(AirPortTester wpas linux80211 NameRegistryLib PCILib)
set_target_properties(AirPortTester PROPERTIES LINK_FLAGS "-Wl,-gc-sections -Wl,-Map=AirPortTester.map")

# ---------------------------------------------------------------------------
# Open Transport driver ('ndrv' for the Extensions folder)
#
# Needs Apple's Universal Interfaces (OpenTransportKernel.h, dlpi.h) and an
# import library for the Open Transport kernel fragments (OTKernelLib,
# OTKernelUtilLib, OTUtilityLib, DLPISupportLib), made with Retro68's
# MakeImport from the "Open Transport" extension of a Mac OS 9 system.
# ---------------------------------------------------------------------------
set(OT_KERNEL_LIB "$ENV{HOME}/retro/otlibs/libOTKernel.a" CACHE FILEPATH
    "Import library of the Open Transport kernel fragments")
set(DRIVER ${CMAKE_CURRENT_SOURCE_DIR}/../AirPortExtremeDriver)
if(EXISTS ${OT_KERNEL_LIB})
    add_ndrv(AirPortExtreme
        EXPORTS ${DRIVER}/AirPortDriver.exp
        CFRAG_NAME "AirPortExtreme"
        CREATOR "APXd"
        ${DRIVER}/AirPortDriver.c
        ${DRIVER}/drv_link.c
        ${DRIVER}/drv_env.c
        ${DRIVER}/drv_pool.c
        ${DRIVER}/drv_prefs.c)
    target_include_directories(AirPortExtreme PRIVATE ${DRIVER} ${COMMON}/os9)
    target_compile_options(AirPortExtreme PRIVATE -std=gnu11 -Os -mtraceback=full -Wall)
    target_link_libraries(AirPortExtreme wpas linux80211 ${OT_KERNEL_LIB}
        DriverServicesLib NameRegistryLib PCILib InterfaceLib)
    target_link_options(AirPortExtreme PRIVATE "LINKER:-Map=AirPortExtreme.map")
else()
    message(WARNING "${OT_KERNEL_LIB} not found: the Open Transport driver is not built")
endif()

# Open Transport diagnostics (console application)
add_application(OTCheck
    ${DRIVER}/OTCheck.c
    CONSOLE)
target_compile_options(OTCheck PRIVATE -std=gnu11 -Os -mtraceback=full -Wall)
target_link_libraries(OTCheck OpenTransportAppPPC OpenTptInetPPC OpenTransportLib
    OpenTptInternetLib)

# Status and log reader for the driver (console application)
add_application(AirPortStatus
    ${DRIVER}/AirPortStatus.c
    CONSOLE)
target_include_directories(AirPortStatus PRIVATE ${DRIVER})
target_compile_options(AirPortStatus PRIVATE -std=gnu11 -Os -mtraceback=full -Wall)
target_link_libraries(AirPortStatus NameRegistryLib)
