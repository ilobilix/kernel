-- Copyright (C) 2024-2025  ilobilo

package("limine")
    add_urls("https://github.com/limine-bootloader/limine.git")
    add_versions("8", "v8.x-binary")

    on_install(function (package)
        local configs = {
            "CC=cc",
            "CFLAGS="
        }
        import("package.tools.make").build(package, configs)

        os.cp("limine", package:installdir("limine-binaries"))
        os.cp("limine.h", package:installdir("include"))
        os.cp("limine-uefi-cd.bin", package:installdir("limine-binaries"))
        os.cp("limine-bios-cd.bin", package:installdir("limine-binaries"))
        os.cp("limine-bios.sys", package:installdir("limine-binaries"))
        os.cp("*.EFI", package:installdir("limine-binaries"))
    end)