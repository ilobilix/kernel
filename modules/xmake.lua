-- Copyright (C) 2024-2025  ilobilo

-- only for the kernel modules
target("modules.dependencies")
    set_kind("phony")

    add_deps("ilobilix.dependencies.nolink")
    add_deps("ilobilix.modules", { inherit = false })

    add_defines(
        "declare_module(name)=[[gnu::used, gnu::section(\".modules\"), gnu::aligned(8)]] const mod::declare name",
        { public = true }
    )

    add_shflags(
        "-nostdlib",
        "-Wl,-znoexecstack",
        "-fpic",
        "-Wl,-T" .. "$(projectdir)/modules/module.ld",
        { force = true, public = true }
    )

    if is_arch("x86_64") then
    elseif is_arch("aarch64") then
    end

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

    after_config(function (target)
        import("core.project.project")
        import("core.project.depend")
        import("core.tool.linker")

        local kernel = project.target("ilobilix.elf")

        for idx, val in ipairs(target:values("modules.deps")) do
            local split = val:trim():split(".", { plain = true })
            if #split > 2 then
                local child = project.target(val)

                child:add("deps", "modules.dependencies")
                child:set("kind", "shared")
                child:set("default", false)

                table.remove(split, 1)
                table.remove(split, 1)

                local values = child:get("values")
                if values ~= nil and values["is_external"] then
                    local targetfile = child:targetfile()
                    local dot_ko = table.concat(split, ".") .. ".ko"

                    depend.on_changed(function ()
                        print(" => external module: " .. dot_ko)
                    end, { files = targetfile })

                    target:add("values", "modules.external_modules", path.join(os.projectdir(), targetfile))
                else
                    local objects_all = child:objectfiles()
                    local objects = os.files(path.join(path.directory(objects_all[1]), "*.o"))

                    depend.on_changed(function ()
                        print(" => internal module: " .. table.concat(split, "."))
                    end, { files = objects })

                    for odx, obj in ipairs(objects) do
                        table.insert(kernel:objectfiles(), obj)
                    end
                end
            end
        end
    end)