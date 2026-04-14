-- Copyright (C) 2024-2025  ilobilo

-- only for the kernel modules
target("modules.dependencies")
    set_kind("phony")

    add_deps("ilobilix.modules", { inherit = false })
    add_deps("ilobilix.dependencies")

    if is_arch("x86_64") then
        local flags = {
            "-mcmodel=large"
        }
        add_cxflags(flags, { force = true, public = true })
        add_asflags(flags, { force = true, public = true })
    elseif is_arch("aarch64") then
    end

target("modules.relocatable")
    set_kind("phony")
    add_ldflags(
        "-nostdlib",
        "-static",
        "-znoexecstack",
        "-relocatable",
        { force = true, public = true }
    )

-- loadable kernel modules, not C++ modules
target("modules")
    set_default(false)
    set_kind("phony")

    set_values("modules.deps", { })
    set_values("modules.external_modules", { })

    if is_arch("x86_64") then
        includes("x86_64")
        add_deps("modules.x86_64")
    elseif is_arch("aarch64") then
        includes("aarch64")
        add_deps("modules.aarch64")
    end

    includes("noarch")
    add_deps("modules.noarch")

    on_build(function (target)
        import("core.project.project")
        import("core.project.depend")
        import("core.tool.linker")

        local kernel = project.target("ilobilix.elf")

        for idx, val in ipairs(target:values("modules.deps")) do
            split = val:trim():split(".", { plain = true })
            if #split > 2 then
                local child = project.target(val)

                if not (child:get("kind") == "object") then
                    raise("please use set_kind(\"object\") for modules")
                end

                local objects_all = child:objectfiles()
                local objects = os.files(path.join(path.directory(objects_all[1]), "*.o"))

                local values = child:get("values", "modules.is_external")
                if values ~= nil and values["modules.is_external"] then
                    -- TODO: clean this up
                    table.remove(split, 1)
                    table.remove(split, 1)
                    local dot_ko = table.concat(split, ".") .. ".ko"
                    local out = path.join(child:targetdir(), dot_ko)

                    depend.on_changed(function ()
                        print(" => external module: " .. val .. " => " .. dot_ko)

                        os.mkdir(path.directory(out))

                        local program, argv = linker.linkargv("binary", "cc", objects, out, { target = project.target("modules.relocatable") })
                        table.insert(argv, "-relocatable")
                        os.execv(program, argv)
                    end, { files = objects })

                    for odx, obj in ipairs(objects) do
                        target:add("values", "modules.external_modules", out)
                    end
                else
                    depend.on_changed(function ()
                        print(" => internal module: " .. val)
                    end, { files = objects })

                    for odx, obj in ipairs(objects) do
                        table.insert(kernel:objectfiles(), obj)
                    end
                end
            end
        end
    end)