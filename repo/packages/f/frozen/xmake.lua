-- Copyright (C) 2024-2025  ilobilo

package("frozen")
    add_urls("https://github.com/serge-sans-paille/frozen.git")
    add_versions("latest", "master")

    on_install(function (package)
        os.cp("include", package:installdir())
    end)