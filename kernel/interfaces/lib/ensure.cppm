// Copyright (C) 2024  ilobilo

export module lib:ensure;
import :panic;
import std;

export namespace lib
{
    template<typename ...Args>
    struct ensure
    {
#if ILOBILIX_DEBUG
        constexpr ensure(std::convertible_to<bool> auto x, std::source_location location = std::source_location::current())
        {
            if (static_cast<bool>(x) == false)
                vpanic("Assertion failed", std::make_format_args(), nullptr, location);
        }

        constexpr ensure(std::convertible_to<bool> auto x, std::string_view message, Args &&...args, std::source_location location = std::source_location::current())
        {
            if (static_cast<bool>(x) == false)
                vpanic(message, std::make_format_args(args...), nullptr, location);
        }
#else
        constexpr ensure(auto &&...) { }
#endif
    };

    template<typename ...Args>
    ensure(std::convertible_to<bool> auto, std::string_view, Args &&...) -> ensure<Args...>;
} // export namespace lib