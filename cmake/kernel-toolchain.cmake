# Copyright (C) 2024-2025  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/shared-toolchain.cmake)

if(ILOBILIX_ARCH STREQUAL "x86_64")
    set(_KERNEL_ARCH_FLAGS
        "-masm=intel"
    )
    string(JOIN " " _KERNEL_ARCH_FLAGS_STR ${_KERNEL_ARCH_FLAGS})
    string(APPEND CMAKE_C_FLAGS " ${_KERNEL_ARCH_FLAGS_STR}")
    string(APPEND CMAKE_CXX_FLAGS " ${_KERNEL_ARCH_FLAGS_STR}")
    string(APPEND CMAKE_ASM_FLAGS " ${_KERNEL_ARCH_FLAGS_STR}")
endif()

set(_KERNEL_FLAGS
    "-fno-PIC"
)
string(JOIN " " _KERNEL_FLAGS_STR ${_KERNEL_FLAGS})
string(APPEND CMAKE_C_FLAGS " ${_KERNEL_FLAGS_STR}")
string(APPEND CMAKE_CXX_FLAGS " ${_KERNEL_FLAGS_STR}")
string(APPEND CMAKE_ASM_FLAGS " ${_KERNEL_FLAGS_STR}")