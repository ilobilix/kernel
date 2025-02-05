-- Copyright (C) 2024-2025  ilobilo

target("printf")
    set_kind("static")
    set_toolchains("ilobilix-clang")
    add_deps("freestnd-c-hdrs")

    add_defines("PRINTF_INCLUDE_CONFIG_H=1")

    add_includedirs("printf/src", { public = true })
    add_includedirs("printf", { public = true })
    add_includedirs(".")
    add_files("printf/src/printf/printf.c")