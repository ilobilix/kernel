-- Copyright (C) 2024-2025  ilobilo

target("uacpi-headers")
    set_kind("headeronly")
    add_includedirs("uACPI/include", { public = true })

target("uacpi")
    set_kind("static")
    set_toolchains("ilobilix-clang")
    add_deps("ilobilix.headers", "freestnd-c-hdrs")

    add_includedirs("uACPI/include", { public = true })
    add_files("uACPI/source/*.c")