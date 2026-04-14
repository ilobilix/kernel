-- Copyright (C) 2024-2025  ilobilo

package("parallel_hashmap")
    add_urls("https://github.com/greg7mdp/parallel-hashmap.git")
    add_versions("latest", "master")

    on_install(function (package)
        os.cp("parallel_hashmap", package:installdir("include"))
    end)