-- Copyright (C) 2024-2025  ilobilo

package("frigg")
    add_urls("https://github.com/managarm/frigg.git")
    add_versions("latest", "master")

    add_deps("freestnd-cxx-hdrs", "freestnd-c-hdrs")

    on_install(function (package)
        os.cp("include", package:installdir())
    end)