-- Copyright (C) 2024-2025  ilobilo

target("limine-headers")
    set_kind("headeronly")
    add_includedirs("limine", { public = true })

target("limine")
    set_default(false)
    set_kind("binary")

    add_includedirs("limine", { public = true })

    on_config(function (target)
        local cd_binaries = {
            "$(projectdir)/dependencies/limine/limine/limine-uefi-cd.bin"
        }
        local uefi_binary = ""
        local bios_sys = ""

        if is_arch("x86_64") then
            table.insert(cd_binaries, "$(projectdir)/dependencies/limine/limine/limine-bios.sys")
            table.insert(cd_binaries, "$(projectdir)/dependencies/limine/limine/limine-bios-cd.bin")
            uefi_binary = "$(projectdir)/dependencies/limine/limine/BOOTX64.EFI"
            bios_sys = "$(projectdir)/dependencies/limine/limine/limine-bios.sys"
        elseif is_arch("aarch64") then
            uefi_binary = "$(projectdir)/dependencies/limine/limine/BOOTAA64.EFI"
        else
            raise("unknown limine architecture")
        end

        target:set("values", "cd-binaries", cd_binaries)
        target:set("values", "uefi-binary", uefi_binary)
        target:set("values", "bios-sys", bios_sys)
    end)

    on_build(function (target)
        local clang = import("lib.detect.find_tool")("clang")
        os.mkdir(path.join(os.projectdir(), path.directory(target:targetfile())))
        os.execv(clang["program"], {
            path.join(os.projectdir(), "dependencies/limine/limine/limine.c"),
            "-o", path.join(os.projectdir(), target:targetfile())
        })
    end)