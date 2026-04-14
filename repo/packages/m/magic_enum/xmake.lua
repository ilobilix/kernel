-- Copyright (C) 2024-2025  ilobilo

package("magic_enum")
    add_urls("https://github.com/Neargye/magic_enum.git")
    add_versions("latest", "master")

    on_install(function (package)
        os.cp("include/magic_enum/*", package:installdir("include"))
    end)