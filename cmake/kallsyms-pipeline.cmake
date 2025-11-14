# Copyright (C) 2024-2025  ilobilo

# a clanker generated this file from my previous xmake.lua script

if(NOT DEFINED TMP_DIR)
    message(FATAL_ERROR "TMP_DIR not set")
endif()

if(NOT DEFINED OUTPUT_ELF OR NOT DEFINED OUTPUT_SYMS)
    message(FATAL_ERROR "OUTPUT_ELF/OUTPUT_SYMS not set")
endif()

get_filename_component(_out_dir "${OUTPUT_ELF}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")

string(REPLACE "|" ";" OBJECT_LIST "${OBJECTS}")
string(REPLACE "|" ";" LINK_LIB_LIST "${LINK_LIBS}")

if(DEFINED LINK_FLAGS AND NOT LINK_FLAGS STREQUAL "")
    separate_arguments(LINK_FLAGS_LIST UNIX_COMMAND "${LINK_FLAGS}")
else()
    set(LINK_FLAGS_LIST)
endif()

if(DEFINED ASM_FLAGS AND NOT ASM_FLAGS STREQUAL "")
    separate_arguments(ASM_FLAGS_LIST UNIX_COMMAND "${ASM_FLAGS}")
else()
    set(ASM_FLAGS_LIST)
endif()

if(DEFINED ASM_INCLUDE_DIR AND NOT ASM_INCLUDE_DIR STREQUAL "")
    list(APPEND ASM_FLAGS_LIST "-I${ASM_INCLUDE_DIR}")
endif()

file(REMOVE_RECURSE "${TMP_DIR}")
file(MAKE_DIRECTORY "${TMP_DIR}")

set(tmp0_syms "${TMP_DIR}/.tmp_ilobilix0.syms")
file(WRITE "${tmp0_syms}" "")

function(run_kallsyms SYMS STAGE_PREFIX OUT_S OUT_O)
    set(stage_S "${TMP_DIR}/${STAGE_PREFIX}.S")
    execute_process(
        # COMMAND "${KALLSYMS_TOOL}" --all-symbols "${SYMS}"
        COMMAND "${KALLSYMS_TOOL}" "${SYMS}"
        OUTPUT_FILE "${stage_S}"
        RESULT_VARIABLE res
    )
    if(res)
        message(FATAL_ERROR "kallsyms failed for ${SYMS}")
    endif()
    set(stage_O "${TMP_DIR}/${STAGE_PREFIX}.o")
    set(cmd "${ASM_COMPILER}")
    list(APPEND cmd ${ASM_FLAGS_LIST})
    list(APPEND cmd -c "${stage_S}" -o "${stage_O}")
    execute_process(COMMAND ${cmd} RESULT_VARIABLE res)
    if(res)
        message(FATAL_ERROR "failed to assemble ${stage_S}")
    endif()
    set(${OUT_S} "${stage_S}" PARENT_SCOPE)
    set(${OUT_O} "${stage_O}" PARENT_SCOPE)
endfunction()

function(link_stage OUTPUT EXTRA_OBJ TAG)
    set(cmd "${LINKER}")
    list(APPEND cmd ${LINK_FLAGS_LIST})
    list(APPEND cmd -o "${OUTPUT}")
    list(APPEND cmd ${OBJECT_LIST})
    if(EXTRA_OBJ)
        list(APPEND cmd "${EXTRA_OBJ}")
    endif()
    list(APPEND cmd ${LINK_LIB_LIST})
    execute_process(COMMAND ${cmd} RESULT_VARIABLE res)
    if(res)
        message(FATAL_ERROR "link failed at ${TAG}")
    endif()
endfunction()

function(make_sysmap IN_ELF OUT_SYMS TAG)
    set(tmp "${OUT_SYMS}.txt")
    execute_process(
        COMMAND "${LLVM_NM}" -n "${IN_ELF}"
        OUTPUT_FILE "${tmp}"
        RESULT_VARIABLE res
    )
    if(res)
        message(FATAL_ERROR "llvm-nm failed at ${TAG}")
    endif()
    execute_process(
        COMMAND "${SED}" "${tmp}" -f "${MKSYSMAP_SCRIPT}"
        OUTPUT_FILE "${OUT_SYMS}"
        RESULT_VARIABLE res
    )
    if(res)
        message(FATAL_ERROR "sed failed at ${TAG}")
    endif()
    file(REMOVE "${tmp}")
endfunction()

run_kallsyms("${tmp0_syms}" "stage0" stage0_S stage0_O)
set(tmp1 "${TMP_DIR}/.tmp_ilobilix1")
link_stage("${tmp1}" "${stage0_O}" "stage0")

set(tmp1_syms "${tmp1}.syms")
make_sysmap("${tmp1}" "${tmp1_syms}" "stage1")
run_kallsyms("${tmp1_syms}" "stage1" stage1_S stage1_O)
set(tmp2 "${TMP_DIR}/.tmp_ilobilix2")
link_stage("${tmp2}" "${stage1_O}" "stage1")

set(tmp2_syms "${tmp2}.syms")
make_sysmap("${tmp2}" "${tmp2_syms}" "stage2")
run_kallsyms("${tmp2_syms}" "stage2" stage2_S stage2_O)
link_stage("${OUTPUT_ELF}" "${stage2_O}" "final")
make_sysmap("${OUTPUT_ELF}" "${OUTPUT_SYMS}" "final")