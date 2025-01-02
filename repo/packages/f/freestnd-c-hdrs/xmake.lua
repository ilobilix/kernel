-- Copyright (C) 2024-2025  ilobilo

package("freestnd-c-hdrs")
    add_urls("https://github.com/osdev0/freestnd-c-hdrs.git")
    add_versions("latest", "trunk")

    on_install(function (package)
        if is_arch("x86_64") then
            os.cp("x86_64/include", package:installdir())
        elseif is_arch("aarch64") then
            os.cp("aarch64/include", package:installdir())
        else
            raise("unknown freestnd-c-hdrs architecture")
        end
    end)