# Copyright (C) 2024-2025  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/module-toolchain.cmake)

set(_module_arches noarch ${ILOBILIX_ARCH})
list(REMOVE_DUPLICATES _module_arches)

set(_module_output_dir ${CMAKE_BINARY_DIR}/modules/modules)
set(_module_targets)
set(_module_objects)

set(_internal_module_targets)
set(_internal_module_objects)

foreach(_arch IN LISTS _module_arches)
    set(_arch_dir ${CMAKE_CURRENT_SOURCE_DIR}/${_arch})
    if(NOT IS_DIRECTORY ${_arch_dir})
        continue()
    endif()

    file(GLOB _subdirs RELATIVE ${_arch_dir} ${_arch_dir}/*)
    foreach(_mod ${_subdirs})
        set(_mod_dir ${_arch_dir}/${_mod})
        if(NOT IS_DIRECTORY ${_mod_dir})
            continue()
        endif()

        file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
            ${_mod_dir}/*.cpp
            ${_mod_dir}/*.cppm
            ${_mod_dir}/*.c
            ${_mod_dir}/*.S
        )
        if(NOT _sources)
            continue()
        endif()

        if(NOT EXISTS ${_mod_dir}/EXTERNAL)
            set_source_files_properties(${_sources}
                PROPERTIES
                    COMPILE_DEFINITIONS "__MODULE_NAME__=${_mod}"
            )

            string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _internal_target "intmod_${_arch}_${_mod}")
            add_library(${_internal_target} OBJECT ${_sources})
            target_include_directories(${_internal_target} PRIVATE ${DEPENDENCY_INCLUDES})
            target_link_libraries(${_internal_target} PRIVATE $<BUILD_INTERFACE:cxx_modules>)

            list(APPEND _internal_module_targets ${_internal_target})
            list(APPEND _internal_module_objects "$<TARGET_OBJECTS:${_internal_target}>")
            continue()
        endif()

        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _target "extmod_${_arch}_${_mod}")
        add_library(${_target} SHARED ${_sources})

        target_compile_definitions(${_target} PRIVATE "__MODULE_NAME__=${_mod}")
        target_link_libraries(${_target} PRIVATE $<BUILD_INTERFACE:cxx_modules>)
        target_include_directories(${_target} PRIVATE ${DEPENDENCY_INCLUDES})

        set(_arch_out ${_module_output_dir}/${_arch})
        file(MAKE_DIRECTORY ${_arch_out})

        set_target_properties(${_target} PROPERTIES
            PREFIX ""
            SUFFIX ".ko"
            OUTPUT_NAME ${_mod}
            LIBRARY_OUTPUT_DIRECTORY ${_arch_out}
            RUNTIME_OUTPUT_DIRECTORY ${_arch_out}
            ARCHIVE_OUTPUT_DIRECTORY ${_arch_out}
        )

        if(CMAKE_CONFIGURATION_TYPES)
            foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
                string(TOUPPER ${_cfg} _cfg_upper)
                set_target_properties(${_target} PROPERTIES
                    LIBRARY_OUTPUT_DIRECTORY_${_cfg_upper} ${_arch_out}
                    RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper} ${_arch_out}
                    ARCHIVE_OUTPUT_DIRECTORY_${_cfg_upper} ${_arch_out}
                )
            endforeach()
        endif()

        list(APPEND _module_targets ${_target})
        list(APPEND _module_objects "$<TARGET_FILE:${_target}>")
    endforeach()
endforeach()

set(INTERNAL_MODULE_TARGETS ${_internal_module_targets} PARENT_SCOPE)
set(INTERNAL_MODULE_OBJECTS ${_internal_module_objects} PARENT_SCOPE)

set(EXTERNAL_MODULE_TARGETS ${_module_targets} PARENT_SCOPE)
set(EXTERNAL_MODULE_OBJECTS ${_module_objects} PARENT_SCOPE)

if(_module_targets)
    add_custom_target(kernel_external_modules ALL DEPENDS ${_module_targets})
else()
    add_custom_target(kernel_external_modules)
endif()