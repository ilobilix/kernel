-- Copyright (C) 2024-2025  ilobilo

target("freestnd-c-hdrs")
    set_kind("phony")

    if is_arch("x86_64") then
        add_includedirs("freestnd-c-hdrs/x86_64/include", { public = true })
    elseif is_arch("aarch64") then
        add_includedirs("freestnd-c-hdrs/aarch64/include", { public = true })
    end