# Copyright (C) 2024-2025  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/kallsyms.cmake)
add_executable(kallsyms ${CMAKE_CURRENT_LIST_DIR}/kallsyms.c)