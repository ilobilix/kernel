-- Copyright (C) 2024-2025  ilobilo

target("limine")
    set_default(false)
    set_kind("binary")

    add_includedirs("limine", { public = true })

    on_config(function (target)
        local binaries = {
            "$(projectdir)/dependencies/limine/limine/limine-uefi-cd.bin"
        }
        local uefi_binaries = { }

        if is_arch("x86_64") then
            table.insert(binaries, "$(projectdir)/dependencies/limine/limine/limine-bios.sys")
            table.insert(binaries, "$(projectdir)/dependencies/limine/limine/limine-bios-cd.bin")
            table.insert(uefi_binaries, "$(projectdir)/dependencies/limine/limine/BOOTX64.EFI")
        elseif is_arch("aarch64") then
            table.insert(uefi_binaries, "$(projectdir)/dependencies/limine/limine/BOOTAA64.EFI")
        else
            raise("unknown limine architecture")
        end

        target:set("values", "binaries", binaries)
        target:set("values", "uefi-binaries", uefi_binaries)
    end)

    on_build(function (target)
        local clang = import("lib.detect.find_tool")("clang")
        os.execv(clang["program"], {
            path.join(os.projectdir(), "dependencies/limine/limine/limine.c"),
            "-o", path.join(os.projectdir(), target:targetfile())
        })
    end)