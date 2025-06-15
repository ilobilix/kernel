-- Copyright (C) 2024-2025  ilobilo

target("flanterm-headers")
    set_kind("headeronly")
    add_includedirs("flanterm", "flanterm/src", { public = true })

target("flanterm")
    set_kind("static")
    set_toolchains("ilobilix-clang")
    add_deps("freestnd-c-hdrs")

    add_includedirs("flanterm", "flanterm/src", { public = true })
    add_files("**.c")