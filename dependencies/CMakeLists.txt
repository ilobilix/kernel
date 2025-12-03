# Copyright (C) 2024-2025  ilobilo

include("${CMAKE_CURRENT_LIST_DIR}/compiler-rt-builtins/compiler-rt-builtins.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cwalk/cwalk.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/demangler/demangler.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/flanterm/flanterm.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/fmt/fmt.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/freestnd-c-hdrs/freestnd-c-hdrs.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/frigg/frigg.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/libstdcxx-headers/libstdcxx-headers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/limine/limine.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/magic_enum/magic_enum.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/parallel-hashmap/phmap.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/string/string.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/uacpi/uacpi.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/veque/veque.cmake")

set(_dependency_includes)
set(_dependency_sources)

get_cmake_property(_all_variables VARIABLES)
foreach(_var ${_all_variables})
    if(_var MATCHES "_INCLUDES$")
        list(APPEND _dependency_includes ${${_var}})
    elseif(_var MATCHES "_SOURCES$")
        list(APPEND _dependency_sources ${${_var}})
    endif()
endforeach()

list(REMOVE_DUPLICATES _dependency_includes)
list(REMOVE_DUPLICATES _dependency_sources)

set(DEPENDENCY_INCLUDES "${_dependency_includes}" CACHE INTERNAL "")
set(DEPENDENCY_SOURCES "${_dependency_sources}" CACHE INTERNAL "")

include(${CMAKE_SOURCE_DIR}/cmake/deps-toolchain.cmake)
add_library(kernel_deps STATIC ${DEPENDENCY_SOURCES})
target_include_directories(kernel_deps PRIVATE ${DEPENDENCY_INCLUDES})