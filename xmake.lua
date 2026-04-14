-- Copyright (C) 2024  ilobilo

set_project("Ilobilix")
set_version("v0.2")

set_license("GPL-3.0")

add_rules("plugin.compile_commands.autoupdate", { outputdir = ".vscode" })

set_policy("run.autobuild", true)
set_policy("package.install_locally", true)
set_policy("build.c++.modules", true)
set_policy("build.c++.modules.std", false)

set_languages("c++23", "c17")

set_allowedarchs("x86_64", "aarch64")
set_defaultarchs("x86_64")

set_allowedplats("ilobilix")
set_defaultplat("ilobilix")

-- debug
if is_mode("debug") then
    set_symbols("debug")
    set_optimize("none")
-- optimised debug
elseif is_mode("releasedbg") then
    set_symbols("debug")
    set_optimize("fastest")
-- release with -Os
elseif is_mode("releasesmall") then
    set_strip("debug")
    set_optimize("smallest")
-- release
elseif is_mode("release") then
    set_strip("debug")
    set_optimize("fastest")
end

set_defaultmode("releasedbg")

-- options -->

option("extra_cflags")
    set_default("")
    set_showmenu(true)
    set_description("Extra CFLAGS")

option("extra_cxxflags")
    set_default("")
    set_showmenu(true)
    set_description("Extra CXXFLAGS")

option("extra_qemuflags")
    set_default("")
    set_showmenu(true)
    set_description("Extra QEMU flags")

option("qemu_proc")
    set_default("6")
    set_showmenu(true)
    set_description("QEMU CPU count")

option("qemu_memory")
    set_default("512M")
    set_showmenu(true)
    set_description("QEMU memory amount")

option("more_panic_msg")
    set_default(true)
    set_showmenu(true)
    set_description("More information when panicking")

option("ubsan")
    set_default(false)
    set_showmenu(true)
    set_description("UBSanitizer in kernel and modules")

option("syscall_log")
    set_default(false)
    set_showmenu(true)
    set_description("Log system calls")

option("qemu_accel")
    set_default(true)
    set_showmenu(true)
    set_description("QEMU accelerators. Disabled when debugging")

option("qemu_gdb")
    set_default(false)
    set_showmenu(true)
    set_description("Pass '-s -S' to QEMU when debugging")

option("qemu_vnc")
    set_default(false)
    set_showmenu(true)
    set_description("Start headless QEMU VNC server on localhost:5901")

-- <-- options

-- variables -->

local function multi_insert(list, ...)
    for idx, val in ipairs({ ... }) do
        list[#list + 1] = val
    end
end

local function get_targetfile(tdir, prefix, ext)
    local ret = path.join(tdir, prefix .. "-" .. get_config("arch") .. ext)
    return ret
end

local logfile = os.projectdir() .. "/log.txt"

local qemu_args = {
    "-rtc", "base=localtime", "-serial", "stdio",
    "-boot", "order=d,menu=on,splash-time=100",
    -- "-drive", "file=" .. os.projectdir() .. "/misc/nvme.img,format=raw,if=none,id=nvm",
    -- "-device", "nvme,serial=nvme,drive=nvm",
    -- "-nic", "user,model=rtl8139",
    -- "-nic", "user,model=e1000"
}

local qemu_accel_args = {
    "-M", "accel=kvm:hvf:whpx:haxm:tcg"
}

local qemu_dbg_args = {
    "-no-reboot", "-no-shutdown",
    "-d", "int", "-D", logfile,
    -- "-monitor", "telnet:127.0.0.1:12345,server,nowait"
}

local ovmf_id = ""
local bios = false

-- <-- variables

-- toolchain -->

toolchain("ilobilix-clang")
    set_kind("standalone")

    set_toolset("as", "clang")
    set_toolset("cc", "clang")
    set_toolset("cxx", "clang++", "clang")

    set_toolset("ld", "ld.lld", "lld")

    set_toolset("ar", "llvm-ar", "ar")
    set_toolset("strip", "llvm-strip", "strip")

    add_defines("ILOBILIX_SYSCALL_LOG=" .. (get_config("syscall_log") and "1" or "0"))
    add_defines("ILOBILIX_EXTRA_PANIC_MSG=" .. (get_config("more_panic_msg") and "1" or "0"))

    add_defines("UACPI_FORMATTED_LOGGING", "UACPI_KERNEL_INITIALIZATION", "UACPI_OVERRIDE_LIBC")
    add_defines("MAGIC_ENUM_NO_STREAMS=1")

    add_defines("FMT_STATIC_THOUSANDS_SEPARATOR=\"'\"", "FMT_USE_LOCALE=0", "FMT_THROW(x)=abort()")
    -- TODO: performance impact
    add_defines("FMT_OPTIMIZE_SIZE=2", "FMT_BUILTIN_TYPES=0")

    on_check(function (toolchain)
        return import("lib.detect.find_tool")("clang")
    end)

    on_load(function (toolchain)
        local cx_args = {
            "-ffreestanding",
            "-fno-stack-protector",
            "-fno-omit-frame-pointer",

            "-nostdinc",

            -- using set_warnings causes some argument ordering issues
            "-Wall", "-Wextra",

            "-Wno-error=#warnings",
            "-Wno-builtin-macro-redefined",
            "-Wno-macro-redefined",
            "-Wno-deprecated-declarations",
            "-Wno-nan-infinity-disabled",
            "-Wno-frame-address"
        }
        local c_args = { }
        local cxx_args = {
            "-fno-rtti",
            "-fno-exceptions",
            "-fsized-deallocation",
        }

        local ld_args = {
            "-nostdlib",
            "-static",
            "-znoexecstack"
        }

        local target = ""

        if is_mode("releasesmall") or is_mode("release") then
            table.insert(cx_args, "-flto")
            toolchain:add("defines", "ILOBILIX_DEBUG=0");
        else
            toolchain:add("defines", "ILOBILIX_DEBUG=1");
        end

        if is_arch("x86_64") then
            target = "x86_64-elf"

            table.insert(cx_args, "-march=x86-64")
            table.insert(cx_args, "-mno-red-zone")
            -- in kernel/xmake.lua
            -- table.insert(cx_args, "-mno-80387")
            table.insert(cx_args, "-mno-mmx")
            table.insert(cx_args, "-mno-sse")
            table.insert(cx_args, "-mno-sse2")
        elseif is_arch("aarch64") then
            target = "aarch64-elf"

            table.insert(cx_args, "-mcmodel=small")

            toolchain:add("defines", "UACPI_REDUCED_HARDWARE")
        end

        table.insert(cx_args, "--target=" .. target)

        table.insert(c_args, get_config("extra_cflags"))
        table.insert(cxx_args, get_config("extra_cxxflags"))

        if get_config("ubsan") then
            table.insert(cx_args, "-fsanitize=undefined")

            toolchain:add("defines", "ILOBILIX_UBSAN")
        end

        toolchain:add("cxxflags", cxx_args, { force = true })
        toolchain:add("cxflags", cx_args, { force = true })
        toolchain:add("cflags", c_args, { force = true })

        toolchain:add("asflags", cxx_args, { force = true })
        toolchain:add("asflags", cx_args, { force = true })
        toolchain:add("asflags", c_args, { force = true })

        toolchain:add("ldflags", ld_args, { force = true })

        toolchain:add("includedirs",
            "$(projectdir)/kernel/include",
            "$(projectdir)/kernel/include/std",
            "$(projectdir)/kernel/include/std/stubs",
            "$(projectdir)/kernel/include/libc",
            "$(projectdir)/kernel/include/kernel",
            "$(projectdir)/kernel/include/kernel/uacpi",
            "$(projectdir)/kernel/include/modules"
        )
    end)
toolchain_end()

-- <-- toolchain

-- dependencies -->

add_toolchains("ilobilix-clang")

add_repositories("local-repo repo")

add_requires(
    "compiler-rt-builtins", "demangler",
    "cwalk", "printf", "uacpi",
    "freestnd-cxx-hdrs", "freestnd-c-hdrs",
    "string", "smart_ptr", "veque", "parallel_hashmap",
    "fmt", "frigg", "frozen", "magic_enum",
    "limine-terminal", "limine", "ovmf-binaries"
)

-- <-- dependencies

-- targets.build -->

includes("modules/xmake.lua")
includes("kernel/xmake.lua")

target("initrd")
    set_default(false)
    set_kind("phony")
    add_deps("modules")

    on_clean(function (target)
        os.tryrm(get_targetfile(target:targetdir(), "initrd", ".img.gz"))
    end)

    on_build(function (target)
        import("core.project.project")
        import("core.project.depend")
        import("lib.detect.find_program")

        -- might not be created yet
        os.mkdir(target:targetdir())

        targetfile = get_targetfile(target:targetdir(), "initrd", ".img.gz")
        target:set("values", "targetfile", targetfile)

        local sysroot = path.join(os.projectdir(), "userspace/sysroot")
        local modules_dir = path.join(sysroot, "usr/lib/modules")

        local modules = project.target("modules")
        local extmods = modules:values("modules.external_modules")
        local created = false

        local function create_initrd()
            os.tryrm(targetfile)
            os.tryrm(modules_dir)
            os.mkdir(modules_dir)

            print(" => copying external modules to sysroot...")

            if extmods ~= nil then
                for idx, val in ipairs(extmods) do
                    os.cp(val, modules_dir)
                end
            end

            print(" => building the initrd...")
            os.execv(find_program("tar"), { "--format", "posix", "-czf", targetfile, "-C", sysroot, "./" })

            created = true
        end

        if extmods ~= nil then
            depend.on_changed(create_initrd, { files = extmods })
        end

        if not created and not os.isfile(targetfile) then
            create_initrd()
        end
    end)

target("iso")
    set_default(false)
    set_kind("phony")
    add_deps("initrd")
    add_deps("modules", "ilobilix.elf")

    add_packages("ovmf-binaries")

    on_clean(function (target)
        os.rm(get_targetfile(target:targetdir(), "image", ".iso"))
    end)

    on_build(function (target)
        import("core.project.project")
        import("core.project.depend")
        import("lib.detect.find_program")

        targetfile = get_targetfile(target:targetdir(), "image", ".iso")
        target:set("values", "targetfile", targetfile)

        local kernel = project.target("ilobilix.elf")
        local initrd = project.target("initrd")

        local iso_dirname = "ilobilix.iso.dir"
        local iso_dir = path.join(os.tmpdir(), iso_dirname)
        local iso_dir_b = path.join(iso_dir, "boot")
        local iso_dir_bl = path.join(iso_dir, "boot/limine")
        local iso_dir_eb = path.join(iso_dir, "EFI/BOOT")

        local limine_binaries = path.join(kernel:pkg("limine"):installdir(), "limine-binaries")
        local limine_exec = path.join(limine_binaries, "limine")

        local limine_files = {
            "$(projectdir)/misc/limine.conf",
            path.join(limine_binaries, "limine-uefi-cd.bin")
        }

        local boot_files = {
            "$(projectdir)/misc/bg.png",
            "$(projectdir)/misc/dtb.img",
            "$(projectdir)/misc/font.bin"
        }

        local efi_files = { }

        local xorriso_args = {
            "-as", "mkisofs"
        }

        if is_arch("x86_64") then
            multi_insert(xorriso_args,
                "-b", "boot/limine/limine-bios-cd.bin",
                "-no-emul-boot", "-boot-load-size", "4",
                "-boot-info-table"
            )

            multi_insert(limine_files,
                path.join(limine_binaries, "limine-bios.sys"),
                path.join(limine_binaries, "limine-bios-cd.bin")
            )
            multi_insert(efi_files,
                path.join(limine_binaries, "BOOTX64.EFI")
                -- path.join(limine_binaries, "BOOTIA32.EFI")
            )
        elseif is_arch("aarch64") then
            table.insert(efi_files, path.join(limine_binaries, "BOOTAA64.EFI"))
        end

        multi_insert(xorriso_args,
            "--efi-boot", "boot/limine/limine-uefi-cd.bin",
            "-efi-boot-part", "--efi-boot-image",
            "--protective-msdos-label"
        )

        local kernelfile = kernel:targetfile()
        local initrdfile = initrd:get("values", "targetfile")["targetfile"]
        local created = false

        local function create_iso()
            os.tryrm(targetfile)
            os.tryrm(iso_dir)

            os.mkdir(iso_dir)
            os.mkdir(iso_dir_b)
            os.mkdir(iso_dir_bl)
            os.mkdir(iso_dir_eb)

            print(" => copying target files to temporary iso directory...")

            for idx, val in ipairs(boot_files) do
                os.cp(val, iso_dir_b)
            end
            for idx, val in ipairs(limine_files) do
                os.cp(val, iso_dir_bl)
            end
            for idx, val in ipairs(efi_files) do
                os.cp(val, iso_dir_eb)
            end
            os.cp(kernelfile, path.join(iso_dir_b, "kernel.elf"))
            os.cp(initrdfile, path.join(iso_dir_b, "initrd.img.gz"))

            multi_insert(xorriso_args,
                iso_dir, "-o", targetfile
            )

            print(" => building the iso...")
            os.execv(find_program("xorriso"), xorriso_args)

            print(" => installing limine...")
            os.execv(limine_exec, { "bios-install", targetfile })

            created = true
            os.tryrm(iso_dir)
        end

        depend.on_changed(create_iso, { files = { kernelfile, initrdfile } })

        if not created and not os.isfile(targetfile) then
            create_iso()
        end
    end)

-- <-- targets.build

-- targets.run -->

task("qemu")
    on_run(function ()
        import("core.base.option")
        import("core.project.project")
        import("lib.detect.find_program")

        local extra_qemu_args = get_config("extra_qemuflags")
        if extra_qemu_args ~= "" then
            table.insert(qemu_args, extra_qemu_args)
        end

        multi_insert(qemu_args, "-smp", get_config("qemu_proc"))
        multi_insert(qemu_args, "-m", get_config("qemu_memory"))

        if get_config("qemu_vnc") then
            multi_insert(qemu_args,
                "-vnc", "127.0.0.1:1"
            )
        end

        local qemu_exec = ""
        if is_arch("x86_64") then
            ovmf_id = "X64"
            bios = true

            multi_insert(qemu_args,
                "-cpu", "max,+hypervisor,+invtsc,+tsc-deadline", "-M", "q35"
                -- "-audiodev", "id=audio,driver=alsa", "-machine", "pcspk-audiodev=audio"
            )

            multi_insert(qemu_dbg_args,
                "-M", "smm=off"
            )

            qemu_exec = find_program("qemu-system-x86_64")
        elseif is_arch("aarch64") then
            ovmf_id = "AA64"
            bios = false

            multi_insert(qemu_args,
                "-cpu", "max", "-M", "virt", "-device", "ramfb"
            )

            qemu_exec = find_program("qemu-system-aarch64")
        end

        if not option.get("uefi") and not bios then
            raise("BIOS not supported on this architecture")
        end

        if option.get("debug") then
            multi_insert(qemu_args, unpack(qemu_dbg_args))
            if get_config("qemu_gdb") then
                multi_insert(qemu_args,
                    "-s", "-S"
                )
            end
        elseif get_config("qemu_accel") then
            multi_insert(qemu_args, unpack(qemu_accel_args))
        end

        local iso = project.target("iso")
        local pkg = iso:pkg("ovmf-binaries"):installdir()
        local ovmf_fd = path.join(pkg, "ovmf-binaries/OVMF_" .. ovmf_id .. ".fd")

        multi_insert(qemu_args,
            "-cdrom", iso:get("values", "targetfile")["targetfile"]
        )

        if option.get("uefi") then
            multi_insert(qemu_args,
                "-bios", ovmf_fd
            )
        end

        print(" => running qemu...")
        os.execv(qemu_exec, qemu_args)
    end)

target("bios")
    set_default(false)
    set_kind("phony")
    add_deps("iso")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = false })
    end)

target("bios-debug")
    set_default(false)
    set_kind("phony")
    add_deps("iso")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = false, debug = true })
    end)

target("uefi")
    set_default(true)
    set_kind("phony")
    add_deps("iso")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = true })
    end)

target("uefi-debug")
    set_default(false)
    set_kind("phony")
    add_deps("iso")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = true, debug = true })
    end)

-- <-- targets.run

-- TODO: https://discord.com/channels/785173387962089492/785369106199216130/1289483363996139621