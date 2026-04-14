-- Copyright (C) 2024-2025  ilobilo

set_project("Ilobilix")
set_version("v0.2")

set_license("EUPL-1.2")

add_rules("plugin.compile_commands.autoupdate", { outputdir = "build", lsp = "clangd" })

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
-- release
elseif is_mode("release") then
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

option("more_panic_msg")
    set_default(true)
    set_showmenu(true)
    set_description("More information when panicking")

option("max_uacpi_points")
    set_default(false )
    set_showmenu(true)
    set_description("Maximise uACPI points")

option("ubsan")
    set_default(false)
    set_showmenu(true)
    set_description("UBSanitizer in kernel and modules")

option("syscall_log")
    set_default(false)
    set_showmenu(true)
    set_description("Log system calls")

option("qemu_smp")
    set_default("6")
    set_showmenu(true)
    set_description("QEMU CPU count")

option("qemu_memory")
    set_default("512M")
    set_showmenu(true)
    set_description("QEMU memory amount")

option("qemu_accel")
    set_default(true)
    set_showmenu(true)
    set_description("QEMU accelerators. Disabled when debugging")

option("qemu_vnc")
    set_default(false)
    set_showmenu(true)
    set_description("Start headless QEMU VNC server on localhost:5901")

option("part_esp_size")
    set_default("32")
    set_showmenu(true)
    set_description("ESP partition size on hdd in MiB")

option("part_root_size")
    set_default("128")
    set_showmenu(true)
    set_description("ROOT partition size on hdd in MiB")

-- <-- options

-- variables -->

local function multi_insert(list, ...)
    for idx, val in ipairs({ ... }) do
        list[#list + 1] = val
    end
end

local function get_arch()
    if is_arch("x86_64") then
        return "x86_64"
    elseif is_arch("aarch64") then
        return "aarch64"
    end
    raise("get_arch(): unsupported architecture")
end

local function userspace_dir()
    return path.join(os.projectdir(), "userspace/" .. get_arch())
end

local logfile = os.projectdir() .. "/log.txt"

local qemu_args = {
    "-rtc", "base=localtime", "-serial", "stdio",
    "-boot", "order=d,menu=on,splash-time=0",
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

local bios = false

-- <-- variables

-- toolchain -->

toolchain("ilobilix-clang")
    set_kind("standalone")

    set_toolset("as", "clang")
    set_toolset("cc", "clang")
    set_toolset("cxx", "clang++")
    set_toolset("sh", "clang++", "clang")

    set_toolset("ld", "ld.lld", "lld")

    set_toolset("ar", "llvm-ar", "ar")
    set_toolset("strip", "llvm-strip", "strip")

    add_defines("ILOBILIX_SYSCALL_LOG=" .. (get_config("syscall_log") and "1" or "0"))
    add_defines("ILOBILIX_EXTRA_PANIC_MSG=" .. (get_config("more_panic_msg") and "1" or "0"))

    add_defines("ILOBILIX_MAX_UACPI_POINTS=" .. (get_config("max_uacpi_points") and "1" or "0"))

    add_defines("LIMINE_API_REVISION=2")
    -- add_defines("FLANTERM_FB_DISABLE_BUMP_ALLOC")

    add_defines("UACPI_OVERRIDE_LIBC", "UACPI_OVERRIDE_ARCH_HELPERS")
    add_defines("MAGIC_ENUM_NO_STREAMS=1")

    add_defines("FMT_USE_LOCALE=0", "FMT_THROW(x)=abort()")
    -- TODO: performance impact
    -- remove FMT_BUILTIN_TYPES=0 for fmt::group_digits
    add_defines("FMT_OPTIMIZE_SIZE=2", "FMT_BUILTIN_TYPES=0")

    add_defines("cpu_local=[[gnu::section(\".percpu\")]] ::cpu::per::storage")
    add_defines("cpu_local_init(name, ...)=" ..
        "void (*name ## _init_func__)(std::uintptr_t) = [](std::uintptr_t base)" ..
            "{ name.initialise_base(base __VA_OPT__(,) __VA_ARGS__); };" ..
        "[[gnu::section(\".percpu_init\"), gnu::used]]" ..
        "const auto name ## _init_ptr__ = name ## _init_func__"
    )

    on_load(function (toolchain)
        local cx_args = {
            "-ffreestanding",
            "-fno-stack-protector",
            "-fno-strict-aliasing",
            "-fstrict-vtable-pointers",
            "-funsigned-char",

            "-mgeneral-regs-only",

            "-nostdinc",

            -- using set_warnings causes some argument ordering issues
            "-Wall", "-Wextra",

            "-Wno-c23-extensions",
            "-Wno-c99-designator"
        }
        local c_args = { }
        local cxx_args = {
            "-fno-rtti",
            "-fno-exceptions",
            "-fsized-deallocation",
            "-fcheck-new",

            "-D__cpp_lib_ranges_to_container=202202L",
            "-D__glibcxx_ranges_to_container=202202L"
        }

        local ld_args = {
            "-nostdlib",
            "-static",
            "-znoexecstack",
            "-zmax-page-size=0x1000"
        }
        local sh_args = {
            "-nostdlib",
            "-fuse-ld=lld",
            "-Wl,-shared"
        }

        local target = ""

        if not get_config("max_uacpi_points") then
            table.insert(cx_args, "-fno-omit-frame-pointer")
        end

        if is_mode("releasesmall") or is_mode("release") then
            if get_config("max_uacpi_points") then
                multi_insert(cx_args,
                    "-flto=full",
                    "-funified-lto"
                )
                table.insert(cxx_args, "-fwhole-program-vtables")
                table.insert(ld_args, "--lto=full")
                multi_insert(sh_args,
                    "-flto=full",
                    "-funified-lto",
                    "-Wl,--lto=full"
                )
            end
            table.insert(ld_args, "--strip-debug")
            toolchain:add("defines", "NDEBUG=1");
            toolchain:add("defines", "ILOBILIX_DEBUG=0");
        else
            toolchain:add("defines", "ILOBILIX_DEBUG=1");
        end

        if is_arch("x86_64") then
            target = "x86_64-elf"

            multi_insert(cx_args,
                "-march=x86-64",
                "-mno-red-zone",
                "-mno-mmx",
                "-mno-sse",
                "-mno-sse2",
                "-mno-80387",
                "-mcmodel=kernel"
            )
        elseif is_arch("aarch64") then
            target = "aarch64-elf"

            table.insert(cx_args, "-mcmodel=small")

            toolchain:add("defines", "UACPI_REDUCED_HARDWARE")
        end

        table.insert(cx_args, "--target=" .. target)
        table.insert(sh_args, "--target=" .. target)

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
        toolchain:add("shflags", sh_args, { force = true })
    end)
toolchain_end()

set_toolchains("ilobilix-clang")

-- <-- toolchain

-- dependencies -->

includes("dependencies/xmake.lua")

-- <-- dependencies

-- targets.build -->

includes("modules/xmake.lua")
includes("kernel/xmake.lua")

target("initramfs")
    set_default(false)
    set_kind("phony")
    add_deps("modules")

    on_clean(function (target)
        os.tryrm(path.join(target:targetdir(), "initramfs.tar"))
    end)

    on_build(function (target)
        import("core.project.project")
        import("core.project.depend")
        import("lib.detect.find_program")

        os.mkdir(target:targetdir())

        targetfile = path.join(target:targetdir(), "initramfs.tar")
        target:set("values", "targetfile", targetfile)

        local initramfs_dir = path.join(userspace_dir(), "initramfs")
        local modules_dir = path.join(initramfs_dir, "usr/lib/modules")

        local modules = project.target("modules")
        local extmods = modules:values("modules.external_modules")
        local created = false

        local function create_initramfs()
            local tar = find_program("tar")
            if tar == nil then
                raise("program 'tar' not found!")
            end

            os.tryrm(targetfile)
            os.tryrm(modules_dir)
            os.mkdir(modules_dir)

            print(" => copying external modules to initramfs...")

            if extmods ~= nil then
                for idx, val in ipairs(extmods) do
                    split = val:trim():split(".", { plain = true })
                    table.remove(split, 1)
                    table.remove(split, 1)
                    table.remove(split, 2)
                    pretty = table.concat(split, ".") .. ".ko"
                    os.cp(val, path.join(modules_dir, pretty))
                end
            end

            print(" => building the initramfs...")
            os.execv(tar, { "--format", "posix", "-cf", targetfile, "-C", initramfs_dir, "./" })

            created = true
        end

        if extmods ~= nil then
            depend.on_changed(create_initramfs, { files = extmods })
        end

        if not created and not os.isfile(targetfile) then
            create_initramfs()
        end
    end)

target("sysroot")
    set_default(false)
    set_kind("phony")

    on_build(function (target)
        os.mkdir(target:targetdir())

        io.writefile(path.join(target:targetdir(), "sysroot_updated"), os.time(os.date("!*t")))

        -- TODO: build the sysroot
    end)

target("iso")
    set_default(false)
    set_kind("phony")
    add_deps("ovmf-binaries", "limine", "initramfs", "ilobilix.elf")

    on_clean(function (target)
        os.rm(path.join(target:targetdir(), "image.iso"))
    end)

    on_build(function (target)
        import("core.project.project")
        import("core.project.depend")
        import("lib.detect.find_program")

        targetfile = path.join(target:targetdir(), "image.iso")
        target:set("values", "targetfile", targetfile)

        local kernel = project.target("ilobilix.elf")
        local initramfs = project.target("initramfs")

        local kernelfile = kernel:targetfile()
        local initramfsfile = initramfs:get("values", "targetfile")["targetfile"]

        local iso_dirname = "ilobilix.iso.dir"
        local iso_dir = path.join(os.tmpdir(), iso_dirname)
        local iso_dir_b = path.join(iso_dir, "boot")
        local iso_dir_bl = path.join(iso_dir, "boot/limine")
        local iso_dir_eb = path.join(iso_dir, "EFI/BOOT")

        local limine_dep = target:deps()["limine"]
        local limine_exec = limine_dep:targetfile()

        local cd_binaries = limine_dep:get("values")["cd-binaries"]
        local uefi_binary = limine_dep:get("values")["uefi-binary"]

        local limine_files = {
            "$(projectdir)/misc/limine.conf"
        }

        local xorriso_args = {
            "-as", "mkisofs"
        }

        if is_arch("x86_64") then
            multi_insert(xorriso_args,
                "-b", "boot/limine/limine-bios-cd.bin",
                "-no-emul-boot", "-boot-load-size", "4",
                "-boot-info-table"
            )
        elseif is_arch("aarch64") then
        end

        multi_insert(xorriso_args,
            "--efi-boot", "boot/limine/limine-uefi-cd.bin",
            "-efi-boot-part", "--efi-boot-image",
            "--protective-msdos-label"
        )

        local created = false

        local function create_iso()
            local xorriso = find_program("xorriso")
            if xorriso == nil then
                raise("program 'xorriso' not found!")
            end

            os.tryrm(targetfile)
            os.tryrm(iso_dir)

            os.mkdir(iso_dir)
            os.mkdir(iso_dir_b)
            os.mkdir(iso_dir_bl)
            os.mkdir(iso_dir_eb)

            print(" => copying target files to temporary iso directory...")

            for idx, val in ipairs(limine_files) do
                os.cp(val, iso_dir_bl)
            end
            for idx, val in ipairs(cd_binaries) do
                os.cp(val, iso_dir_bl)
            end
            for idx, val in ipairs(uefi_binary) do
                os.cp(val, iso_dir_eb)
            end
            os.cp(kernelfile, path.join(iso_dir_b, "kernel.elf"))
            os.cp(initramfsfile, path.join(iso_dir_b, "initramfs.tar"))

            multi_insert(xorriso_args,
                iso_dir, "-o", targetfile
            )

            print(" => building the iso...")
            os.execv(xorriso, xorriso_args)

            print(" => installing limine...")
            os.execv(limine_exec, { "bios-install", targetfile })

            created = true
            os.tryrm(iso_dir)
        end

        depend.on_changed(create_iso, { files = { kernelfile, initramfsfile } })

        if not created and not os.isfile(targetfile) then
            create_iso()
        end
    end)

target("hdd")
    set_default(false)
    set_kind("phony")
    add_deps("ovmf-binaries", "limine", "ilobilix.elf", "initramfs")

    on_clean(function (target)
        os.rm(path.join(target:targetdir(), "image.hdd"))
    end)

    on_build(function (target)
        import("core.project.project")
        import("core.project.depend")
        import("lib.detect.find_program")

        targetfile = path.join(target:targetdir(), "image.hdd")
        target:set("values", "targetfile", targetfile)

        local esp_img = path.join(target:targetdir(), "esp.img")
        local root_img = path.join(target:targetdir(), "root.img")

        local old_esp_size = path.join(target:targetdir(), "esp.size")
        local old_root_size = path.join(target:targetdir(), "root.size")

        local kernel = project.target("ilobilix.elf")
        local initramfs = project.target("initramfs")

        local kernelfile = kernel:targetfile()
        local initramfsfile = initramfs:get("values", "targetfile")["targetfile"]

        local sysroot_updatefile = path.join(target:targetdir(), "sysroot_updated")
        if not os.exists(sysroot_updatefile) then
            io.writefile(sysroot_updatefile, os.time(os.date("!*t")))
        end

        local sysroot_dir = path.join(userspace_dir(), "sysroot")
        os.mkdir(sysroot_dir)

        local limine_dep = target:deps()["limine"]
        local limine_exec = limine_dep:targetfile()

        local uefi_binary = limine_dep:get("values")["uefi-binary"]
        local bios_sys = limine_dep:get("values")["bios-sys"]

        local limine_conf = path.join(os.projectdir(), "/misc/limine.conf")

        try {
            function ()
                local function find(name)
                    local ret = find_program(name)
                    if ret == nil then
                        if os.isexec("/usr/bin/" .. name) then
                            ret = "/usr/bin/" .. name
                        else
                            raise("program '" .. name .. "' not found!")
                        end
                    end
                    return ret
                end

                local truncate = find("truncate")
                local dd = find("dd")
                local parted = find("parted")
                local mformat = find("mformat")
                local mcopy = find("mcopy")
                local mmd = find("mmd")
                local mkfs_ext4 = find("mkfs.ext4")

                local esp_size = tonumber(get_config("part_esp_size"))
                local root_size = tonumber(get_config("part_root_size"))

                local function check_and_write(path, expected)
                    if not os.isfile(path) then
                        io.writefile(path, expected)
                    end
                    local data = tonumber(io.readfile(path))
                    if data ~= expected then
                        io.writefile(path, expected)
                    end
                end

                check_and_write(old_esp_size, esp_size)
                check_and_write(old_root_size, root_size)

                local function remove_trailing(num)
                    return string.format("%u", num)
                end

                local disk_size = remove_trailing(esp_size + root_size + 2)

                local esp_end_sector_num = ((esp_size + 1) * 1024 * 1024) / 512 - 1
                local esp_end_sector = remove_trailing(esp_end_sector_num)
                local esp_end_sectorp1 = remove_trailing(esp_end_sector_num + 1)

                local root_end_sector = remove_trailing(esp_end_sector_num + ((root_size * 1024 * 1024) / 512))

                local needs_esp_update = false
                local needs_root_update = false
                local size_changed = false

                local function create_hdd()
                    os.tryrm(targetfile)

                    print(" => creating empty image.hdd...")
                    os.execv(truncate, { "-s" .. disk_size .. "M", targetfile })

                    print(" => partitioning image.hdd...")
                    os.execv(parted, { "-s", targetfile, "mklabel", "gpt" })
                    os.execv(parted, { "-s", targetfile, "-a", "none", "mkpart", "ESP", "fat32", "2048s", esp_end_sector .. "s" })
                    os.execv(parted, { "-s", targetfile, "set", "1", "esp", "on" })
                    os.execv(parted, { "-s", targetfile, "-a", "none", "mkpart", "ROOT", "ext4", esp_end_sectorp1 .. "s", root_end_sector .. "s" })

                    if is_arch("x86_64") then
                        print(" => running limine bios-install on image.hdd")
                        os.execv(limine_exec, { "bios-install", targetfile })
                    end
                end

                local function create_esp()
                    os.tryrm(esp_img)

                    print(" => creating empty esp.img...")
                    os.execv(truncate, { "-s" .. esp_size .. "M", esp_img })

                    print(" => formatting esp.img...")
                    os.execv(mformat, { "-i", esp_img })

                    print(" => copying necessary files to esp.img...")
                    os.execv(mmd, { "-i", esp_img, "::/EFI", "::/EFI/BOOT", "::/boot", "::/boot/limine" })
                    os.execv(mcopy, { "-i", esp_img, kernelfile, "::/boot/kernel.elf" })
                    os.execv(mcopy, { "-i", esp_img, initramfsfile, "::/boot/initramfs.tar" })
                    os.execv(mcopy, { "-i", esp_img, limine_conf, "::/boot/limine/limine.conf" })
                    if is_arch("x86_64") then
                        os.execv(mcopy, { "-i", esp_img, bios_sys, "::/boot/limine/limine-bios.sys" })
                    end
                    os.execv(mcopy, { "-i", esp_img, uefi_binary, "::/EFI/BOOT" })

                    needs_esp_update = true
                end

                local function copy_esp()
                    print(" => copying esp.img to image.hdd...")
                    os.execv(dd, { "if=" .. esp_img, "of=" .. targetfile, "bs=512", "seek=2048", "conv=notrunc,sparse" })
                end

                local function create_root()
                    os.tryrm(root_img)

                    print(" => creating empty root.img...")
                    os.execv(truncate, { "-s" .. root_size .. "M", root_img })

                    print(" => partitioning root.img and copying sysroot...")
                    os.execv(mkfs_ext4, { "-d", sysroot_dir, root_img })

                    needs_root_update = true
                end

                local function copy_root()
                    print(" => copying root.img to image.hdd...")
                    os.execv(dd, { "if=" .. root_img, "of=" .. targetfile, "bs=512", "seek=" .. esp_end_sectorp1, "conv=notrunc,sparse" })
                end

                local function handle_size()
                    size_changed = true
                    create_hdd()
                end

                depend.on_changed(handle_size, { files = { old_esp_size, old_root_size }  })
                depend.on_changed(create_esp, { files = { kernelfile, initramfsfile, limine_conf, old_esp_size } })
                depend.on_changed(create_root, { files = { sysroot_updatefile, old_root_size } })

                if size_changed then
                    copy_esp()
                    copy_root()
                else
                    if needs_esp_update then
                        copy_esp()
                    end
                    if needs_root_update then
                        copy_root()
                    end
                end
            end,
            catch {
                function (errors)
                    os.tryrm(targetfile)
                    os.tryrm(esp_img)
                    os.tryrm(root_img)
                    raise(errors)
                end
            }
        }
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

        multi_insert(qemu_args, "-smp", get_config("qemu_smp"))
        multi_insert(qemu_args, "-m", get_config("qemu_memory"))

        if get_config("qemu_vnc") then
            multi_insert(qemu_args,
                "-vnc", "127.0.0.1:1"
            )
        end

        local qemu_exec = ""
        if is_arch("x86_64") then
            bios = true

            multi_insert(qemu_args,
                "-cpu", "max,migratable=off,+invtsc,+tsc-deadline", "-M", "q35,smm=off"
                -- "-audiodev", "id=audio,driver=alsa", "-machine", "pcspk-audiodev=audio"
            )

            qemu_exec = find_program("qemu-system-x86_64")
        elseif is_arch("aarch64") then
            bios = false

            multi_insert(qemu_args,
                "-cpu", "cortex-a72", "-M", "virt", "-device", "ramfb"
            )

            qemu_exec = find_program("qemu-system-aarch64")
        end

        if qemu_exec == nil then
            raise("could not find qemu for the current architecture!")
        end

        if not option.get("uefi") and not bios then
            raise("BIOS not supported on this architecture")
        end

        if option.get("debug") then
            multi_insert(qemu_args, unpack(qemu_dbg_args))
            if option.get("gdb") then
                multi_insert(qemu_args,
                    "-s", "-S"
                )
            end
        elseif get_config("qemu_accel") then
            multi_insert(qemu_args, unpack(qemu_accel_args))
        end

        -- local iso = project.target("iso")
        -- local ovmf_fd = iso:deps()["ovmf-binaries"]:get("values")["ovmf-binary"]

        -- multi_insert(qemu_args,
        --     "-cdrom", iso:get("values", "targetfile")["targetfile"]
        -- )

        local hdd = project.target("hdd")
        local ovmf_fd = hdd:deps()["ovmf-binaries"]:get("values")["ovmf-binary"]

        multi_insert(qemu_args,
            "-drive",
            "file=" .. hdd:get("values", "targetfile")["targetfile"] ..
            ",format=raw,media=disk"
        )

        if option.get("uefi") then
            multi_insert(qemu_args,
                "-bios", ovmf_fd
            )
        end

        local cxxfilt = path.join(os.projectdir(), "misc/cxxfilt.sh")

        print(" => running qemu...")
        os.execv(cxxfilt, { qemu_exec, unpack(qemu_args) })
    end)

target("bios")
    set_default(false)
    set_kind("phony")
    add_deps("hdd")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = false })
    end)

target("bios-debug")
    set_default(false)
    set_kind("phony")
    add_deps("hdd")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = false, debug = true })
    end)

target("bios-gdb")
    set_default(false)
    set_kind("phony")
    add_deps("hdd")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = false, debug = true, gdb = true })
    end)

target("uefi")
    set_default(true)
    set_kind("phony")
    add_deps("hdd")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = true })
    end)

target("uefi-debug")
    set_default(false)
    set_kind("phony")
    add_deps("hdd")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = true, debug = true })
    end)

target("uefi-gdb")
    set_default(false)
    set_kind("phony")
    add_deps("hdd")

    on_run(function (target)
        import("core.project.task")
        import("core.project.project")
        task.run("qemu", { uefi = true, debug = true, gdb = true })
    end)

-- <-- targets.run

-- TODO: https://discord.com/channels/785173387962089492/785369106199216130/1289483363996139621