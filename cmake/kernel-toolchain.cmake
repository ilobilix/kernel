# Copyright (C) 2024-2025  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/shared-toolchain.cmake)

if(ILOBILIX_ARCH STREQUAL "x86_64")
    string(APPEND CMAKE_EXE_LINKER_FLAGS
        " -Wl,-T${CMAKE_SOURCE_DIR}/kernel/linker-x86_64.ld"
    )
elseif(ILOBILIX_ARCH STREQUAL "aarch64")
    string(APPEND CMAKE_EXE_LINKER_FLAGS
        " -Wl,-T${CMAKE_SOURCE_DIR}/kernel/linker-aarch64.ld"
    )
else()
    message(FATAL_ERROR "Unsupported ILOBILIX_ARCH: ${ILOBILIX_ARCH}")
endif()

set(_KERNEL_FLAGS
    "-fno-pic"
    "-fno-pie"
)
ilobilix_append_flags("C;CXX;ASM" ${_KERNEL_FLAGS})