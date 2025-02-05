-- Copyright (C) 2024-2025  ilobilo

target("ovmf-binaries")
    set_kind("phony")

    on_build(function (target)
        if is_arch("x86_64") then
            target:set("values", "ovmf-binary", "$(projectdir)/dependencies/ovmf-binaries/ovmf-binaries/OVMF_X64.fd")
        elseif is_arch("aarch64") then
            target:set("values", "ovmf-binary", "$(projectdir)/dependencies/ovmf-binaries/ovmf-binaries/OVMF_AA64.fd")
        else
            raise("unknown ovmf architecture")
        end
    end)