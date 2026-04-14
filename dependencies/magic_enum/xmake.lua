-- Copyright (C) 2024-2025  ilobilo

target("magic_enum")
    set_kind("phony")
    add_includedirs("magic_enum/include/magic_enum", { public = true })