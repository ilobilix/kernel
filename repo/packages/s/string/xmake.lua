-- Copyright (C) 2024-2025  ilobilo

package("string")
    add_urls("https://github.com/ilobilo/string.git")
    add_versions("latest", "master")

    on_install(function (package)
        os.cp("include", package:installdir())
    end)