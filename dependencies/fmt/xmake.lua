-- Copyright (C) 2024-2025  ilobilo

target("fmt")
    set_kind("static")
    set_toolchains("ilobilix-clang")
    add_deps("ilobilix.headers", "freestnd-cxx-hdrs", "freestnd-c-hdrs", "smart-ptr", "string", "veque")

    add_includedirs("fmt/include", { public = true })
    add_files("fmt/src/format.cc")