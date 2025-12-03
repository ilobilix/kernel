// Copyright (C) 2024-2025  ilobilo

export module lib:bug_on;

import :panic;
import :types;
import std;
namespace lib
{
    constexpr comptime_string bug_on_message { "BUG BUG BUG!!!!!" };
} // namespace lib

export namespace lib
{
#if ILOBILIX_DEBUG
    template<typename ...Args>
    struct bug_on : panic_base<bug_on_message, false, Args...>
    {
        using panic_base<bug_on_message, false, Args...>::panic_base;
    };
#else
    template<typename ...Args>
    struct bug_on
    {
        constexpr bug_on(auto &&...) { }
    };
#endif

    template<typename ...Args>
    bug_on(bool) -> bug_on<Args...>;
    template<typename ...Args>
    bug_on(bool, std::string_view, Args &&...) -> bug_on<Args...>;
} // export namespace lib