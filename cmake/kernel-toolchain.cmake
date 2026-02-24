# Copyright (C) 2024-2026  ilobilo

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

set(_ILOBILIX_KERNEL_DEFINES
    "cpu_local=\
        [[gnu::section(\".percpu\")]] \
        ::cpu::local::storage"
    "cpu_local_init(name, ...)=\
        void (*name ## _init_func__)(std::uintptr_t) = [](std::uintptr_t base) { \
            name.initialise_base(base __VA_OPT__(,) __VA_ARGS__)^ \
        }^ \
        [[gnu::section(\".percpu_init\"), gnu::used]] \
        const auto name ## _init_ptr__ = name ## _init_func__"
)

foreach(_kernel_define ${_ILOBILIX_KERNEL_DEFINES})
    string (REPLACE "^" "\;" _replaced_kernel_define "${_kernel_define}")
    ilobilix_append_flags("CXX" "-D'${_replaced_kernel_define}'")
endforeach()