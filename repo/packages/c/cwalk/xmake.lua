-- Copyright (C) 2024  ilobilo

package("cwalk")
    add_urls("https://github.com/likle/cwalk.git")
    add_versions("latest", "master")

    add_deps("freestnd-c-hdrs")

    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_requires("freestnd-c-hdrs")
            target("cwalk")
                add_packages("freestnd-c-hdrs")

                set_kind("static")
                set_languages("gnu17")

                add_includedirs("include")
                add_files("src/cwalk.c")
        ]])
        local configs = { }
        import("package.tools.xmake").install(package, configs)
        os.cp("include", package:installdir())
    end)