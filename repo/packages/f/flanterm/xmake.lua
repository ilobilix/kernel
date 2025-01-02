-- Copyright (C) 2024-2025  ilobilo

package("flanterm")
    add_urls("https://github.com/mintsuki/flanterm.git")
    add_versions("latest", "trunk")

    add_deps("freestnd-c-hdrs")

    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_requires("freestnd-c-hdrs")
            target("limine-terminal")
                add_packages("freestnd-c-hdrs")

                set_kind("static")

                add_includedirs("fonts", "src")
                add_files(
                    "flanterm.c",
                    "backends/fb.c"
                )
        ]])
        local configs = { }
        import("package.tools.xmake").install(package, configs)
        os.cp("flanterm.h", package:installdir("include"))
        os.cp("backends", package:installdir("include"))
    end)