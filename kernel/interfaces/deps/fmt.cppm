// Copyright (C) 2024-2025  ilobilo

module;

#include <fmt/color.h>
#include <fmt/ranges.h>
#include <fmt/compile.h>

export module fmt;

export namespace fmt
{
    using ::fmt::basic_format_arg;
    using ::fmt::basic_format_args;
    using ::fmt::basic_format_context;
    using ::fmt::basic_format_parse_context;

    using ::fmt::format_args;
    using ::fmt::format_context;
    using ::fmt::format_parse_context;

    using ::fmt::format_string;

    using ::fmt::formatter;

    using ::fmt::format;

    using ::fmt::vformat;
    using ::fmt::vformat_to;

    using ::fmt::format_to;
    using ::fmt::format_to_n;
    using ::fmt::format_to_n_result;

    using ::fmt::formatted_size;

    using ::fmt::make_format_args;

    inline namespace literals
    {
        using ::fmt::literals::operator""_cf;
    } // inline namespace literals
} // export namespace fmt