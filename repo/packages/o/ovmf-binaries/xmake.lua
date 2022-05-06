-- Copyright (C) 2024  ilobilo

package("ovmf-binaries")
    add_urls("https://github.com/ilobilo/ovmf-binaries.git")
    add_versions("latest", "master")

    on_install(function (package)
        if is_arch("x86_64") then
            os.cp("OVMF_X64.fd", package:installdir("ovmf-binaries"))
        elseif is_arch("aarch64") then
            os.cp("OVMF_AA64.fd", package:installdir("ovmf-binaries"))
        else
            raise("unknown ovmf architecture")
        end
    end)