-- Copyright (C) 2024-2025  ilobilo

target("cwalk-headers")
    set_kind("headeronly")
    add_deps("ilobilix.headers")
    add_includedirs("cwalk/include", { public = true })

target("cwalk")
    set_kind("static")
    set_toolchains("ilobilix-clang")
    add_deps("ilobilix.headers")

    add_includedirs("cwalk/include", { public = true })
    add_files("cwalk/src/cwalk.c")