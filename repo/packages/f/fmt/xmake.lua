-- Copyright (C) 2024-2025  ilobilo

package("fmt")
    add_urls("https://github.com/ilobilo/fmt.git")
    add_versions("latest", "master")

    add_deps("freestnd-cxx-hdrs", "freestnd-c-hdrs", "smart_ptr", "string", "veque")

    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_requires("freestnd-cxx-hdrs", "freestnd-c-hdrs", "smart_ptr", "string", "veque")
            target("fmt")
                set_kind("static")
                add_packages("freestnd-cxx-hdrs", "freestnd-c-hdrs", "smart_ptr", "string", "veque")

                set_languages("c++23")

                add_includedirs("include")
                add_files("src/format.cc")
        ]])
        local configs = { }
        import("package.tools.xmake").install(package, configs)
        os.cp("include", package:installdir())
    end)