# Copyright (C) 2024-2026  ilobilo

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${SRC_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(GIT_COMMIT_HASH)
    set(ILOBILIX_RELEASE "${PROJECT_VERSION}-${GIT_COMMIT_HASH}")
else()
    set(ILOBILIX_RELEASE "${PROJECT_VERSION}")
endif()

set(VERSION_CONTENT "#pragma once\n#define ILOBILIX_COMMIT \"${GIT_COMMIT_HASH}\"\n#define ILOBILIX_RELEASE \"${ILOBILIX_RELEASE}\"\n")

set(SHOULD_WRITE TRUE)
if(EXISTS "${BIN_DIR}/generated/version.h")
    file(READ "${BIN_DIR}/generated/version.h" OLD_VERSION_CONTENT)
    if("${OLD_VERSION_CONTENT}" STREQUAL "${VERSION_CONTENT}")
        set(SHOULD_WRITE FALSE)
    endif()
endif()

if(SHOULD_WRITE)
    message(STATUS "updating version.h: ${ILOBILIX_RELEASE}")
    file(WRITE "${BIN_DIR}/generated/version.h" "${VERSION_CONTENT}")
endif()
