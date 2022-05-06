-- Copyright (C) 2024  ilobilo

package("uacpi")
    add_urls("https://github.com/UltraOS/uACPI.git")
    add_versions("latest", "master")

    add_deps("freestnd-c-hdrs")

    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_requires("freestnd-c-hdrs")
            target("uacpi")
                add_packages("freestnd-c-hdrs")

                set_kind("static")
                set_languages("gnu17")

                add_includedirs("include")
                add_files("source/*.c")
        ]])
        local configs = { }
        import("package.tools.xmake").install(package, configs)
        os.cp("include", package:installdir())
    end)