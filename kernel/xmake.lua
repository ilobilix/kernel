-- Copyright (C) 2024-2025  ilobilo

target("ilobilix.headers")
    set_kind("headeronly")
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

target("ilobilix.dependencies.base")
    set_kind("phony")

    add_deps("ilobilix.headers")
    add_deps(
        "frigg", "frozen", "magic_enum",
        "parallel-hashmap",
        "hosted-libstdcxx", "string", "veque"
    )

    if is_arch("x86_64") then
        local flags = {
            "-masm=intel"
        }
        add_cxflags(flags, { force = true, public = true })
        add_asflags(flags, { force = true, public = true })
    elseif is_arch("aarch64") then
    end

target("ilobilix.dependencies")
    set_kind("phony")

    add_deps("ilobilix.dependencies.base")
    add_deps(
        "compiler-rt-builtins", "cwalk",
        "flanterm", "fmt",
        "printf", "limine", "uacpi"
    )

target("ilobilix.dependencies.nolink")
    set_kind("phony")

    add_deps("ilobilix.dependencies.base")
    add_deps(
        "cwalk-headers",
        "flanterm-headers", "fmt-headers",
        "printf-headers", "limine-headers",
        "uacpi-headers"
    )

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

target("kallsyms")
    set_default(false)
    set_kind("binary")

    on_build(function (target)
        import("core.project.project")
        import("lib.detect.find_tool")

        local clang = find_tool("clang")
        os.mkdir(path.join(os.projectdir(), path.directory(target:targetfile())))
        os.execv(clang["program"], {
            path.join(os.projectdir(), "misc/kallsyms.c"),
            "-D" .. project.target("ilobilix.elf"):get("defines", "KSYM_NAME_LEN"),
            "-o", path.join(os.projectdir(), target:targetfile())
        })
    end)

target("kallsyms-syms")
    set_default(false)
    set_kind("binary")
    add_deps("ilobilix.headers")

target("ilobilix.elf")
    set_default(false)
    set_kind("binary")

    add_deps("modules", { inherit = false })
    add_deps("ilobilix.modules")
    add_deps("kallsyms")

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

    -- :lapfedmoment:
    if is_mode("debug") then
        add_defines("KSYM_NAME_LEN=4096")
    else
        add_defines("KSYM_NAME_LEN=2048")
    end

    on_link(function (target)
        import("core.project.project")
        import("core.project.depend")
        import("core.tool.linker")

        local objects = target:objectfiles()
        local targetelf = target:targetfile()

        depend.on_changed(function ()
            local kallsymso = ""

            local tmpdir = path.join(os.tmpdir(), "kallsyms")
            os.tryrm(tmpdir)
            os.mkdir(tmpdir)

            local function kallsyms(syms)
                import("core.project.project")
                import("core.tool.compiler")

                local wdir = path.join(tmpdir, "tmp")
                os.tryrm(wdir)
                os.mkdir(wdir)

                local filenameS = path.join(wdir, path.basename(syms) .. ".S")

                local exec = path.absolute(project.target("kallsyms"):targetfile(), os.projectdir())
                os.execv(exec, { syms }, { stdout = filenameS })

                local filenameO = path.join(tmpdir, path.basename(filenameS) .. ".o")
                compiler.compile(filenameS, filenameO, { target = project.target("kallsyms-syms") })

                os.rm(wdir)
                kallsymso = filenameO
            end

            local function mksysmap(inelf, outsyms)
                local outsymstmp = outsyms .. ".txt"
                os.execv("llvm-nm", { "-C", "-n", inelf }, { stdout = outsymstmp })
                os.execv("sed", { outsymstmp, "-f", path.join(os.projectdir(), "misc/mksysmap") }, { stdout = outsyms })
                os.rm(outsymstmp)
            end

            local function sysmap_and_kallsyms(name)
                mksysmap(name, name .. ".syms")
                kallsyms(name .. ".syms")
            end

            local function link(tgt)
                local objs = { }
                for idx, val in ipairs(objects) do
                    objs[#objs + 1] = val
                end
                table.insert(objs, kallsymso)
                linker.link("binary", "cxx", objs, tgt, { target = target })
            end

            local tmp_ilobilix0 = path.join(tmpdir, ".tmp_ilobilix0")
            local tmp_ilobilix1 = path.join(tmpdir, ".tmp_ilobilix1")
            local tmp_ilobilix2 = path.join(tmpdir, ".tmp_ilobilix2")
            local tmp_ilobilix3 = path.join(tmpdir, ".tmp_ilobilix3")

            local tmp_ilobilix0_syms = tmp_ilobilix0 .. ".syms"
            local file = io.open(tmp_ilobilix0_syms, "w")
            file:write("")
            file:close()

            print(" => kallsyms step 0...")
            kallsyms(tmp_ilobilix0_syms)
            link(tmp_ilobilix1)

            print(" => kallsyms step 1...")
            sysmap_and_kallsyms(tmp_ilobilix1)
            link(tmp_ilobilix2)

            print(" => kallsyms step 2...")
            sysmap_and_kallsyms(tmp_ilobilix2)
            link(tmp_ilobilix3)

            print(" => kallsyms step 3...")
            sysmap_and_kallsyms(tmp_ilobilix3)
            link(tmp_ilobilix3)

            os.cp(tmp_ilobilix3, targetelf)
            -- TODO: verify

            os.rm(tmpdir)
        end, { files = objects })
    end)

    on_run(function (target)
    end)