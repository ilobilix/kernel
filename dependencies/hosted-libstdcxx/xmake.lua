-- Copyright (C) 2024-2025  ilobilo

target("hosted-libstdcxx")
    set_kind("phony")
    add_includedirs("hosted-libstdcxx/include", { public = true })