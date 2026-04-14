# Copyright (C) 2024-2025  ilobilo

set(DEMANGLER_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/demangler/include" CACHE PATH "")
set(DEMANGLER_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/demangler/source/ItaniumDemangle.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/demangler/source/cxa_demangle.cpp"
    CACHE STRING ""
)