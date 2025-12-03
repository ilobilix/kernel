# Copyright (C) 2024-2025  ilobilo

set(LIBSTDCXX_HEADERS_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/libstdcxx-headers/include" CACHE PATH "")
set(LIBSTDCXX_HEADERS_BASE_DIR "${CMAKE_CURRENT_LIST_DIR}/libstdcxx-headers/src")
set(LIBSTDCXX_HEADERS_MODULES
    "${CMAKE_CURRENT_LIST_DIR}/libstdcxx-headers/src/std.cc"
    "${CMAKE_CURRENT_LIST_DIR}/libstdcxx-headers/src/std.compat.cc"
)