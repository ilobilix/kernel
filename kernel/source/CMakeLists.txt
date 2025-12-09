# Copyright (C) 2024-2025  ilobilo

include(${CMAKE_SOURCE_DIR}/cmake/kernel-toolchain.cmake)

find_program(LLVM_NM_EXECUTABLE llvm-nm REQUIRED)
find_program(SED_EXECUTABLE sed REQUIRED)
find_program(GREP_EXECUTABLE grep REQUIRED)
find_program(SORT_EXECUTABLE sort REQUIRED)
find_program(AWK_EXECUTABLE awk REQUIRED)

file(GLOB_RECURSE KERNEL_SOURCE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)
list(FILTER KERNEL_SOURCE_FILES EXCLUDE REGEX "/arch/")

file(GLOB_RECURSE KERNEL_ARCH_SOURCE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/${ILOBILIX_ARCH}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/${ILOBILIX_ARCH}/*.S
)
list(APPEND KERNEL_SOURCE_FILES ${KERNEL_ARCH_SOURCE_FILES})
list(REMOVE_DUPLICATES KERNEL_SOURCE_FILES)

add_library(kernel_objs OBJECT ${KERNEL_SOURCE_FILES})

target_include_directories(kernel_objs PRIVATE ${DEPENDENCY_INCLUDES})
target_link_libraries(kernel_objs PRIVATE cxx_modules kernel_deps)

set(_kernel_objects "$<TARGET_OBJECTS:kernel_objs>")
if(INTERNAL_MODULE_OBJECTS)
    list(APPEND _kernel_objects ${INTERNAL_MODULE_OBJECTS})
endif()
list(JOIN _kernel_objects "|" KERNEL_OBJECTS_JOINED)

set(KERNEL_LINK_LIBS cxx_modules kernel_deps)
set(KERNEL_LINK_LIBS_FILES "")
foreach(lib IN LISTS KERNEL_LINK_LIBS)
    if(TARGET ${lib})
        list(APPEND KERNEL_LINK_LIBS_FILES "$<TARGET_FILE:${lib}>")
    else()
        list(APPEND KERNEL_LINK_LIBS_FILES "${lib}")
    endif()
endforeach()
list(JOIN KERNEL_LINK_LIBS_FILES "|" KERNEL_LINK_LIBS_JOINED)

set(KERNEL_OUTPUT_ELF ${CMAKE_CURRENT_BINARY_DIR}/kernel_elf)
set(KERNEL_OUTPUT_SYMS ${KERNEL_OUTPUT_ELF}.syms)
set(KERNEL_TMPDIR ${CMAKE_CURRENT_BINARY_DIR}/kallsyms.tmp)

string(STRIP "${CMAKE_ASM_FLAGS}" KERNEL_ASM_FLAGS)
string(STRIP "${CMAKE_EXE_LINKER_FLAGS}" KERNEL_LINK_FLAGS)

set(KERNEL_EXTRA_DEPENDS "")

if(_ILOBILIX_ENABLE_LTO)
    set(RETAIN_SYMBOLS_FILE "${CMAKE_CURRENT_BINARY_DIR}/modules.retain.txt")

    add_custom_command(
        OUTPUT ${RETAIN_SYMBOLS_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo "Generating symbol retention file..."
        COMMAND ${LLVM_NM_EXECUTABLE} --defined-only --extern-only --format=just-symbols ${_kernel_objects} | ${SORT_EXECUTABLE} -u > ${CMAKE_CURRENT_BINARY_DIR}/kernel.defined.txt
        COMMAND ${LLVM_NM_EXECUTABLE} -u ${EXTERNAL_MODULE_OBJECTS} | ${AWK_EXECUTABLE} "/ U / {print \$NF}" | ${SORT_EXECUTABLE} -u > ${CMAKE_CURRENT_BINARY_DIR}/modules.needed.txt
        COMMAND ${GREP_EXECUTABLE} -Fxf ${CMAKE_CURRENT_BINARY_DIR}/kernel.defined.txt ${CMAKE_CURRENT_BINARY_DIR}/modules.needed.txt | ${SED_EXECUTABLE} "s/^/-Wl,--undefined=/" | tr "\\n" " " > ${RETAIN_SYMBOLS_FILE}
        DEPENDS kernel_objs ${EXTERNAL_MODULE_TARGETS}
        COMMENT "Generating LTO symbol retention file"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    string(APPEND KERNEL_LINK_FLAGS " @${RETAIN_SYMBOLS_FILE}")

    add_custom_target(retain_symbols ALL
        DEPENDS ${RETAIN_SYMBOLS_FILE}
    )
    list(APPEND KERNEL_EXTRA_DEPENDS retain_symbols)
endif()

add_custom_command(
    OUTPUT ${KERNEL_OUTPUT_ELF} ${KERNEL_OUTPUT_SYMS}
    COMMAND ${CMAKE_COMMAND}
        -DOBJECTS=${KERNEL_OBJECTS_JOINED}
        -DLINK_LIBS=${KERNEL_LINK_LIBS_JOINED}
        -DLINK_FLAGS=${KERNEL_LINK_FLAGS}
        -DASM_FLAGS=${KERNEL_ASM_FLAGS}
        -DASM_INCLUDE_DIR=${CMAKE_SOURCE_DIR}/kernel/include/libc
        -DKALLSYMS_TOOL=$<TARGET_FILE:kallsyms>
        -DLLVM_NM=${LLVM_NM_EXECUTABLE}
        -DSED=${SED_EXECUTABLE}
        -DMKSYSMAP_SCRIPT=${CMAKE_SOURCE_DIR}/misc/mksysmap
        -DASM_COMPILER=${CMAKE_ASM_COMPILER}
        -DLINKER=${CMAKE_CXX_COMPILER}
        -DOUTPUT_ELF=${KERNEL_OUTPUT_ELF}
        -DOUTPUT_SYMS=${KERNEL_OUTPUT_SYMS}
        -DTMP_DIR=${KERNEL_TMPDIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/kallsyms-pipeline.cmake
    DEPENDS
        ${_kernel_objects}
        kernel_objs
        kallsyms
        ${KERNEL_EXTRA_DEPENDS}
        ${KERNEL_LINK_LIBS}
        ${INTERNAL_MODULE_TARGETS}
    BYPRODUCTS
        ${KERNEL_TMPDIR}/.tmp_ilobilix1
        ${KERNEL_TMPDIR}/.tmp_ilobilix2
    VERBATIM
)

add_custom_target(kernel ALL DEPENDS ${KERNEL_OUTPUT_ELF})
add_custom_target(kernel_elf_syms DEPENDS ${KERNEL_OUTPUT_SYMS})

add_dependencies(kernel kernel_elf_syms)
add_dependencies(kernel kernel_external_modules)