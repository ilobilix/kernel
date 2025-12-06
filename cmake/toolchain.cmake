# Copyright (C) 2024-2025  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/build-type.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/arch.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/config.cmake)

set(_C_CXX_ASM_FLAGS
    "--target=${ILOBILIX_ARCH}-elf"

    "-ffreestanding"
    "-nostdinc"

    "-fno-stack-protector"
    "-fstrict-vtable-pointers"
    "-funsigned-char"

    "-mgeneral-regs-only"

    "-Wall" "-Wextra"
    "-Wno-c23-extensions"
    "-Wno-c99-designator"
    "-Wno-unknown-attributes"
    "-Wno-unused-command-line-argument"
    "-Wno-deprecated-declarations"
)

set(_CXX_FLAGS
    "-fno-rtti"
    "-fno-exceptions"
    "-fsized-deallocation"
    "-fcheck-new"
    "-D__cpp_lib_ranges_to_container=202202L"
    "-D__glibcxx_ranges_to_container=202202L"
    "-D__cpp_lib_constexpr_string=201907L"
)

if(ILOBILIX_ARCH STREQUAL "x86_64")
    list(APPEND _C_CXX_ASM_FLAGS
        "-march=x86-64"
        "-mno-red-zone"
        "-mno-mmx"
        "-mno-sse"
        "-mno-sse2"
        "-mno-80387"
        "-mcmodel=kernel"
        "-mstack-alignment=8"
    )
elseif(ILOBILIX_ARCH STREQUAL "aarch64")
    list(APPEND _C_CXX_ASM_FLAGS
        "-mcmodel=small"
        "-DUACPI_REDUCED_HARDWARE"
    )
else()
    message(FATAL_ERROR "Unsupported ILOBILIX_ARCH: ${ILOBILIX_ARCH}")
endif()

if(ILOBILIX_UBSAN)
    list(APPEND _C_CXX_ASM_FLAGS "-fsanitize=undefined")
endif()

if(NOT ILOBILIX_MAX_UACPI_POINTS)
    list(APPEND _C_CXX_ASM_FLAGS "-fno-omit-frame-pointer")
endif()

if(CMAKE_BUILD_TYPE MATCHES "^(ReleaseDbg|Release)$")
    list(APPEND _C_CXX_ASM_FLAGS "-O3")
endif()

if(CMAKE_BUILD_TYPE MATCHES "^(Debug|ReleaseDbg)$")
    list(APPEND _C_CXX_ASM_FLAGS "-DILOBILIX_DEBUG=1 -g")
else()
    list(APPEND _C_CXX_ASM_FLAGS "-DILOBILIX_DEBUG=0" "-DNDEBUG=1")
endif()

set(_ILOBILIX_ENABLE_LTO FALSE)
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND ILOBILIX_MAX_UACPI_POINTS)
    set(_ILOBILIX_ENABLE_LTO TRUE)
    list(APPEND _C_CXX_ASM_FLAGS "-flto=full" "-funified-lto")
    list(APPEND _CXX_FLAGS "-fwhole-program-vtables")
endif()

list(APPEND _C_CXX_ASM_FLAGS "-DILOBILIX_ARCH='\"${ILOBILIX_ARCH}\"'")
list(APPEND _C_CXX_ASM_FLAGS "-DILOBILIX_SYSNAME='\"${ILOBILIX_SYSNAME}\"'")

set(_ILOBILIX_BOOL_DEFINES
    "ILOBILIX_SYSCALL_LOG:ILOBILIX_SYSCALL_LOG"
    "ILOBILIX_EXTRA_PANIC_MSG:ILOBILIX_EXTRA_PANIC_MSG"
    "ILOBILIX_MAX_UACPI_POINTS:ILOBILIX_MAX_UACPI_POINTS"
    "ILOBILIX_LIMINE_MP:ILOBILIX_LIMINE_MP"
    "ILOBILIX_UBSAN:ILOBILIX_UBSAN"
)

foreach(_define ${_ILOBILIX_BOOL_DEFINES})
    string(REPLACE ":" ";" _parts "${_define}")
    list(GET _parts 0 _macro)
    list(GET _parts 1 _opt)
    if(${_opt})
        set(_value 1)
    else()
        set(_value 0)
    endif()
    list(APPEND _C_CXX_ASM_FLAGS "-D${_macro}=${_value}")
endforeach()

set(_ILOBILIX_DEFINES
    "LIMINE_API_REVISION=2"
    "UACPI_OVERRIDE_LIBC"
    "UACPI_OVERRIDE_ARCH_HELPERS"
    "MAGIC_ENUM_NO_STREAMS=1"
    "MAGIC_ENUM_ENABLE_HASH=1"
    "FMT_THROW(x)=abort()"
    "FMT_USE_CONSTEVAL=1"
    "FMT_USE_CONSTEXPR_STRING=1"
    # "FMT_USE_LOCALE=0"
    "FMT_OPTIMIZE_SIZE=2"
    "FMT_BUILTIN_TYPES=0"
    "FMT_FULLY_FREESTANDING=1"
    "__user=__attribute__((address_space(1)))"
    "__force=__attribute__((force))"
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

foreach(_define ${_ILOBILIX_DEFINES})
    string (REPLACE "^" "\;" _replaced_define "${_define}")
    list(APPEND _C_CXX_ASM_FLAGS "-D'${_replaced_define}'")
endforeach()

string(JOIN " " _C_CXX_ASM_FLAGS_STR ${_C_CXX_ASM_FLAGS})
string(JOIN " " _CXX_FLAGS_STR ${_CXX_FLAGS})

set(CMAKE_C_FLAGS "${_C_CXX_ASM_FLAGS_STR}")
set(CMAKE_CXX_FLAGS "${_C_CXX_ASM_FLAGS_STR} ${_CXX_FLAGS_STR}")
set(CMAKE_ASM_FLAGS "${_C_CXX_ASM_FLAGS_STR}")

include(${CMAKE_SOURCE_DIR}/cmake/kallsyms.cmake)

set(_BASE_LINKER_FLAGS
    "-fuse-ld=lld"
    "-nostdlib"
    "-Wl,-znoexecstack"
    "-Wl,-zmax-page-size=0x1000"
)

if(_ILOBILIX_ENABLE_LTO)
    list(APPEND _BASE_LINKER_FLAGS
        "-flto=full"
        "-funified-lto"
        "-Wl,--lto=full"
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    list(APPEND _BASE_LINKER_FLAGS "-Wl,--strip-debug")
endif()

set(_EXE_LINKER_FLAGS
    ${_BASE_LINKER_FLAGS}
    "-static"
    "-Wl,-static"
)

set(_SHARED_LINKER_FLAGS
    ${_BASE_LINKER_FLAGS}
    "-shared"
    "-Wl,-shared"
)

string(JOIN " " _EXE_LINKER_FLAGS_STR ${_EXE_LINKER_FLAGS})
string(JOIN " " _SHARED_LINKER_FLAGS_STR ${_SHARED_LINKER_FLAGS})

set(CMAKE_EXE_LINKER_FLAGS "${_EXE_LINKER_FLAGS_STR}")
set(CMAKE_SHARED_LINKER_FLAGS "${_SHARED_LINKER_FLAGS_STR}")

include_directories(
    ${CMAKE_SOURCE_DIR}/kernel/include
    ${CMAKE_SOURCE_DIR}/kernel/include/std
    ${CMAKE_SOURCE_DIR}/kernel/include/std/stubs
    ${CMAKE_SOURCE_DIR}/kernel/include/libc
    ${CMAKE_SOURCE_DIR}/kernel/include/kernel
    ${CMAKE_SOURCE_DIR}/kernel/include/kernel/uacpi
)

function(ilobilix_append_flags LANGUAGES)
    foreach(_lang IN LISTS LANGUAGES)
        set(_var "CMAKE_${_lang}_FLAGS")
        foreach(_flag IN LISTS ARGN)
            if(${_var})
                set(${_var} "${${_var}} ${_flag}")
            else()
                set(${_var} "${_flag}")
            endif()
        endforeach()
        set(${_var} "${${_var}}" PARENT_SCOPE)
    endforeach()
endfunction()