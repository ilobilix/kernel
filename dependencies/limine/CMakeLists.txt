# Copyright (C) 2024-2025  ilobilo

add_executable(limine ${CMAKE_CURRENT_LIST_DIR}/limine/limine.c)
target_include_directories(limine PRIVATE ${CMAKE_CURRENT_LIST_DIR}/limine)