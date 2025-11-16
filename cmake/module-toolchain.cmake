# Copyright (C) 2024-2025  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/shared-toolchain.cmake)

set(_MOD_FLAGS
    "-fpic"
    "-fno-plt"
)
ilobilix_append_flags("C;CXX;ASM" ${_MOD_FLAGS})

set(_MOD_CXX_DEFINES
    "-D'__CONCAT_(x, y)=x ## y'"
    "-D'__CONCAT(x, y)=__CONCAT_(x, y)'"
    "-D'define_module=\
        [[gnu::used, gnu::section(\".modules\"), gnu::aligned(8)]] \
        constexpr mod::declare __CONCAT(__CONCAT(__MODULE_NAME__, __COUNTER__), __LINE__)'"
)
ilobilix_append_flags("CXX" ${_MOD_CXX_DEFINES})

string(APPEND CMAKE_SHARED_LINKER_FLAGS
    " -Wl,-T${CMAKE_SOURCE_DIR}/modules/module.ld"
)