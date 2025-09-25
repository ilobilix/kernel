// Copyright (C) 2024-2025  ilobilo

export module lib:bug_on;
import :panic;
import cppstd;

export namespace lib
{
    template<typename ...Args>
    class bug_on
    {
#if ILOBILIX_DEBUG
        private:
        class value
        {
            private:
            bool val;

            public:
            constexpr value(std::convertible_to<bool> auto x)
                : val { static_cast<bool>(x) } { }

            template<typename Type, typename Err>
            constexpr value(std::expected<Type, Err> &&x)
                : val { x.has_value() } { }

            constexpr operator bool() const { return val; }
        };

        public:
        constexpr bug_on(
            value condition,
            std::source_location location = std::source_location::current()
        ) {
            if (condition)
                vpanic("BUG BUG BUG!!!!!", std::make_format_args(), nullptr, location);
        }

        constexpr bug_on(
            value condition, std::string_view message, Args &&...args,
            std::source_location location = std::source_location::current()
        ) {
            if (condition)
                vpanic(message, std::make_format_args(args...), nullptr, location);
        }
#else
        public:
        constexpr bug_on(auto &&...) { }
#endif
    };

    template<typename ...Args>
    bug_on(std::convertible_to<bool> auto, std::string_view, Args &&...) -> bug_on<Args...>;
} // export namespace lib