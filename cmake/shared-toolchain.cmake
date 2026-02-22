# Copyright (C) 2024-2026  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/toolchain.cmake)

if(ILOBILIX_ARCH STREQUAL "x86_64")
    set(_SHARED_ARCH_FLAGS
        "-masm=intel"
    )
    ilobilix_append_flags("C;CXX;ASM" ${_SHARED_ARCH_FLAGS})
endif()