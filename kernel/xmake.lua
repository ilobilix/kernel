-- Copyright (C) 2024-2025  ilobilo

target("ilobilix.headers")
    set_kind("phony")
    add_includedirs(
        "$(projectdir)/kernel/include",
        "$(projectdir)/kernel/include/std",
        "$(projectdir)/kernel/include/std/stubs",
        "$(projectdir)/kernel/include/libc",
        "$(projectdir)/kernel/include/kernel",
        "$(projectdir)/kernel/include/kernel/uacpi",
        { public = true }
    )
    add_deps("freestnd-cxx-hdrs", "freestnd-c-hdrs")

target("ilobilix.dependencies")
    set_kind("phony")

    add_deps("ilobilix.headers")
    add_deps(
        "compiler-rt-builtins", "cwalk",
        "demangler", "flanterm", "fmt",
        "frigg", "frozen", "magic_enum",
        "parallel-hashmap", "printf", "limine",
        "smart_ptr", "string", "uacpi", "veque"
    )

    if is_arch("x86_64") then
        local flags = {
            "-masm=intel"
        }
        add_cxflags(flags, { force = true, public = true })
        add_asflags(flags, { force = true, public = true })
    elseif is_arch("aarch64") then
    end

target("ilobilix.modules")
    set_default(false)
    set_kind("static")

    add_deps("ilobilix.dependencies")

    add_files("interfaces/**.cppm", { public = true })

    if not is_arch("x86_64") then
        remove_files("interfaces/arch/x86_64/**.cppm")
    end
    if not is_arch("aarch64") then
        remove_files("interfaces/arch/aarch64/**.cppm")
    end

    on_run(function (target)
    end)

target("ilobilix.elf")
    set_default(false)
    set_kind("binary")

    add_deps("modules", { inherit = false })
    add_deps("ilobilix.modules")

    add_files("source/**.cpp")
    add_files("source/**.S")

    if not is_arch("x86_64") then
        remove_files("source/arch/x86_64/**.cpp")
        remove_files("source/arch/x86_64/**.S")
    end
    if not is_arch("aarch64") then
        remove_files("source/arch/aarch64/**.cpp")
        remove_files("source/arch/aarch64/**.S")
    end

    add_ldflags(
        "-zmax-page-size=0x1000",
        { force = true }
    )

    if is_arch("x86_64") then
        add_ldflags(
            "-T" .. "$(projectdir)/kernel/linker-x86_64.ld",
            { force = true }
        )
    elseif is_arch("aarch64") then
        add_ldflags(
            "-T" .. "$(projectdir)/kernel/linker-aarch64.ld",
            { force = true }
        )
    end

    on_run(function (target)
    end)