# Copyright (C) 2024-2025  ilobilo

cmake_minimum_required(VERSION 3.28)
project(ilobilix
    VERSION 0.0.1
    LANGUAGES CXX C ASM
)

set(CMAKE_PROJECT_LICENSE "EUPL-1.2")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_SCAN_FOR_MODULES)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS ON)

add_subdirectory(dependencies/limine)
add_subdirectory(dependencies/kallsyms)

add_subdirectory(dependencies)
add_subdirectory(kernel/interfaces)
add_subdirectory(kernel/source)