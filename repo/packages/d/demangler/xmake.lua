-- Copyright (C) 2024  ilobilo

package("demangler")
    add_urls("https://github.com/ilobilo/demangler.git")
    add_versions("latest", "master")

    add_deps("freestnd-cxx-hdrs", "freestnd-c-hdrs", "string", "smart_ptr")

    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_requires("freestnd-cxx-hdrs", "freestnd-c-hdrs", "string", "smart_ptr")
            target("demangler")
                set_kind("static")
                add_packages("freestnd-cxx-hdrs", "freestnd-c-hdrs", "string", "smart_ptr")

                set_languages("c++23")

                add_includedirs("include")
                add_files(
                    "source/ItaniumDemangle.cpp",
                    "source/cxa_demangle.cpp"
                )
        ]])
        local configs = { }
        import("package.tools.xmake").install(package, configs)
        os.cp("include", package:installdir())
    end)
