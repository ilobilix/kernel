-- Copyright (C) 2024-2025  ilobilo

target("flanterm")
    set_kind("static")
    set_toolchains("ilobilix-clang")
    add_deps("freestnd-c-hdrs")

    add_files("**.c")
    add_includedirs("flanterm", "flanterm/backends", { public = true })