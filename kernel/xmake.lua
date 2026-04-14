-- Copyright (C) 2024  ilobilo

-- shared between kernel and drivers
target("ilobilix.dependencies")
    set_kind("phony")
    set_toolchains("ilobilix-clang", { public = true })

    add_packages(
        "compiler-rt-builtins", "demangler",
        "cwalk", "printf", "uacpi",
        "freestnd-cxx-hdrs", "freestnd-c-hdrs",
        "string", "smart_ptr", "veque", "parallel_hashmap",
        "fmt", "frigg", "frozen", "magic_enum",
        "limine-terminal", "limine",
        { public = true }
    )

    add_cxflags(
        "-mgeneral-regs-only",
        { force = true, public = true }
    )

    if is_arch("x86_64") then
        local flags = {
            "-mno-80387",
            "-masm=intel",
        }
        add_cxflags(flags, { force = true, public = true })
        add_asflags(flags, { force = true, public = true })
    elseif is_arch("aarch64") then
    end

-- only for kernel
target("ilobilix.kernel.dependencies")
    set_kind("phony")
    add_deps("ilobilix.dependencies")

    if is_arch("x86_64") then
        local flags = {
            "-mcmodel=kernel"
        }
        add_cxflags(flags, { force = true, public = true })
        add_asflags(flags, { force = true, public = true })
    elseif is_arch("aarch64") then
    end

-- C++ modules, not kernel drivers
target("ilobilix.modules")
    set_default(false)
    set_kind("static")

    add_deps("ilobilix.kernel.dependencies")

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

    add_files("**.cpp")
    add_files("**.S")

    if not is_arch("x86_64") then
        remove_files("source/arch/x86_64/**.cpp")
        remove_files("source/arch/x86_64/**.S")
    end
    if not is_arch("aarch64") then
        remove_files("source/arch/aarch64/**.cpp")
        remove_files("source/arch/aarch64/**.S")
    end

    -- linker flags
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