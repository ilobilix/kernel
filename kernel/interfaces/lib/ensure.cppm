// Copyright (C) 2024-2025  ilobilo

export module lib:ensure;
import :panic;
import cppstd;

export namespace lib
{
    template<typename ...Args>
    struct ensure
    {
#if ILOBILIX_DEBUG
        class value
        {
            private:
            bool val;

            public:
            constexpr value(std::convertible_to<bool> auto x) : val(static_cast<bool>(x)) { }

            template<typename Type, typename Err>
            constexpr value(std::expected<Type, Err> &&x) : val(x.has_value()) { }

            constexpr operator bool() const { return val; }
        };

        constexpr ensure(value x, std::source_location location = std::source_location::current())
        {
            if (!x)
                vpanic("Assertion failed", std::make_format_args(), nullptr, location);
        }

        constexpr ensure(value x, std::string_view message, Args &&...args, std::source_location location = std::source_location::current())
        {
            if (!x)
                vpanic(message, std::make_format_args(args...), nullptr, location);
        }
#else
        constexpr ensure(auto &&...) { }
#endif
    };

    template<typename ...Args>
    ensure(std::convertible_to<bool> auto, std::string_view, Args &&...) -> ensure<Args...>;
} // export namespace lib