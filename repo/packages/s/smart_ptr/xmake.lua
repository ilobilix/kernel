-- Copyright (C) 2024  ilobilo

package("smart_ptr")
    add_urls("https://github.com/ilobilo/smart_ptr.git")
    add_versions("latest", "master")

    on_install(function (package)
        os.cp("include", package:installdir())
    end)