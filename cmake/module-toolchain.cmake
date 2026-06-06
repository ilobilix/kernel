# Copyright (C) 2024-2026  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/shared-toolchain.cmake)

set(_MOD_FLAGS
    "-fpic"
    "-fno-plt"
)
ilobilix_append_flags("C;CXX;ASM" ${_MOD_FLAGS})

ilobilix_append_flags("CXX" "-include ${CMAKE_SOURCE_DIR}/modules/module.h")

string(APPEND CMAKE_SHARED_LINKER_FLAGS
    " -Wl,-T${CMAKE_SOURCE_DIR}/modules/module.ld"
)
