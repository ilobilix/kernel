-- Copyright (C) 2024  ilobilo

package("printf")
    add_urls("https://github.com/eyalroz/printf.git")
    add_versions("latest", "master")

    add_deps("freestnd-c-hdrs")

    on_install(function (package)
        io.writefile("src/printf_config.h", [[
            #pragma once

            #ifndef PRINTF_CONFIG_H_
            #define PRINTF_CONFIG_H_

            #define PRINTF_SUPPORT_DECIMAL_SPECIFIERS 0
            #define PRINTF_SUPPORT_EXPONENTIAL_SPECIFIERS 0
            #define PRINTF_SUPPORT_WRITEBACK_SPECIFIER 1
            #define PRINTF_SUPPORT_MSVC_STYLE_INTEGER_SPECIFIERS 1
            #define PRINTF_SUPPORT_LONG_LONG 1

            #define PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_SOFT 0
            #define PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD 0

            #define PRINTF_INTEGER_BUFFER_SIZE 32
            #define PRINTF_DECIMAL_BUFFER_SIZE 32
            #define PRINTF_DEFAULT_FLOAT_PRECISION 6
            #define PRINTF_MAX_INTEGRAL_DIGITS_FOR_DECIMAL 9
            #define PRINTF_LOG10_TAYLOR_TERMS 4
            #define PRINTF_CHECK_FOR_NUL_IN_FORMAT_SPECIFIER 1

            #endif // PRINTF_CONFIG_H_
        ]])
        io.writefile("xmake.lua", [[
            add_requires("freestnd-c-hdrs")
            target("printf")
                add_packages("freestnd-c-hdrs")

                set_kind("static")
                set_languages("c99")

                add_defines("PRINTF_INCLUDE_CONFIG_H=1")

                add_includedirs("src")
                add_includedirs(".")
                add_files("src/printf/printf.c")
        ]])
        local configs = { }
        import("package.tools.xmake").install(package, configs)
        os.cp("src/printf/printf.h", package:installdir("include/printf"))
    end)