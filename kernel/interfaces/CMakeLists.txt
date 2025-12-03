# Copyright (C) 2024-2025  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/shared-toolchain.cmake)
include(${CMAKE_SOURCE_DIR}/dependencies/libstdcxx-headers/libstdcxx-headers.cmake)

file(GLOB_RECURSE CXX_MODULE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cppm
)
list(FILTER CXX_MODULE_FILES EXCLUDE REGEX "/arch/")

file(GLOB_RECURSE ARCH_CXX_MODULE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/${ILOBILIX_ARCH}/*.cppm
)
list(APPEND CXX_MODULE_FILES ${ARCH_CXX_MODULE_FILES})
list(APPEND CXX_MODULE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/arch/arch.cppm)
list(REMOVE_DUPLICATES CXX_MODULE_FILES)

add_library(cxx_modules)
set_target_properties(cxx_modules PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_include_directories(cxx_modules PRIVATE ${DEPENDENCY_INCLUDES})
target_sources(cxx_modules
    PUBLIC
        FILE_SET cxx_modules
        TYPE CXX_MODULES
        FILES ${CXX_MODULE_FILES}
)
target_sources(cxx_modules
    PUBLIC
        FILE_SET cxx_std_modules
        TYPE CXX_MODULES
        BASE_DIRS ${LIBSTDCXX_HEADERS_BASE_DIR}
        FILES ${LIBSTDCXX_HEADERS_MODULES}
)