-- Copyright (C) 2024  ilobilo

package("freestnd-cxx-hdrs")
    add_urls("https://github.com/osdev0/freestnd-cxx-hdrs.git")
    add_versions("latest", "trunk")

    on_install(function (package)
        if is_arch("x86_64") then
            os.cp("x86_64/include", package:installdir())
        elseif is_arch("aarch64") then
            os.cp("aarch64/include", package:installdir())
        else
            raise("unknown freestnd-cxx-hdrs architecture")
        end
    end)