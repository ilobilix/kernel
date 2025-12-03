// Copyright (C) 2024-2025  ilobilo

export module lib:log;

import :spinlock;
import :math;
import fmt;
import std;

namespace lib::log
{
    constinit lib::spinlock_preempt _lock;
    std::uint64_t get_time();

    export namespace unsafe
    {
        void prints(std::string_view str);
        void printc(char chr);

        inline void lock() { _lock.lock(); }
        inline void unlock() { _lock.unlock(); }
    } // namespace unsafe

    namespace detail
    {
        // no memory allocation needed
        inline void vprint(std::string_view fmt, fmt::format_args args)
        {
            struct {
                using value_type = char;
                void push_back(char chr) { unsafe::printc(chr); }
            } printer;

            fmt::vformat_to(std::back_inserter(printer), fmt, args);
        }

        template<typename ...Args> requires (sizeof...(Args) > 0)
        inline void print(std::string_view fmt, Args &&...args)
        {
            if constexpr (sizeof...(Args) == 1 && std::same_as<std::remove_cvref_t<std::tuple_element_t<0, std::tuple<Args...>>>, fmt::format_args>)
                vprint(fmt, args...);
            else
                vprint(fmt, fmt::make_format_args(args...));
        }

        template<typename ...Args>
        inline void print(std::string_view fmt, Args &&...)
        {
            unsafe::prints(fmt);
        }
    } // namespace detail
} // namespace lib::log

export namespace lib::log
{
    enum class log_level
    {
#if ILOBILIX_DEBUG
        debug,
#endif
        info,
        warn,
        error,
        fatal
    };

    constexpr std::string_view reset_colour { "\e[0m" };
    constexpr std::string_view colours[]
    {
#if ILOBILIX_DEBUG
        "\e[90m",
#endif
        "\e[92m",
        "\e[33m",
        "\e[91m",
        "\e[41m"
    };

    constexpr std::string_view prefix[]
    {
#if ILOBILIX_DEBUG
        "debug",
#endif
        "info",
        "warn",
        "error",
        "fatal"
    };

    inline void print(std::string_view fmt, auto &&...args)
    {
        const std::unique_lock _ { _lock };
        detail::print(fmt, args...);
    }

    namespace unsafe
    {
        inline constexpr void print_nolock(log_level lvl, std::string_view fmt, auto &&...args)
        {
            const auto index = std::to_underlying(lvl);

            auto n = get_time();
            const auto [h, m, s] = lib::time_from(n / 1'000'000'000);
            n %= 1'000'000'000;
            n /= 1'000;

            detail::print("[{:02}:{:02}:{:02}.{:06}] [{}{}{}] ", h, m, s, n, colours[index], prefix[index], reset_colour);
            detail::print(fmt, args...);
        }
    } // namespace unsafe

    inline constexpr void print(log_level lvl, std::string_view fmt, auto &&...args)
    {
        const std::unique_lock _ { _lock };
        unsafe::print_nolock(lvl, fmt, args...);
    }

    inline void println(std::string_view fmt = "", auto &&...args)
    {
        const std::unique_lock _ { _lock };
        detail::print(fmt, args...);
        unsafe::printc('\n');
    }

    inline constexpr void println(log_level lvl, std::string_view fmt = "", auto &&...args)
    {
        const std::unique_lock _ { _lock };
        unsafe::print_nolock(lvl, fmt, args...);
        unsafe::printc('\n');
    }

#if ILOBILIX_DEBUG
    inline constexpr void debug(std::string_view fmt, auto &&...args) { println(log_level::debug, fmt, args...); }
#else
    inline constexpr void debug(std::string_view, auto &&...) { }
#endif
    inline constexpr void info (std::string_view fmt, auto &&...args) { println(log_level::info,  fmt, args...); }
    inline constexpr void warn (std::string_view fmt, auto &&...args) { println(log_level::warn,  fmt, args...); }
    inline constexpr void error(std::string_view fmt, auto &&...args) { println(log_level::error, fmt, args...); }
    inline constexpr void fatal(std::string_view fmt, auto &&...args) { println(log_level::fatal, fmt, args...); }
} // export namespace lib::log

export namespace lib
{
    using log::log_level;

    using log::print;
    using log::println;

    using log::debug;
    using log::info;
    using log::warn;
    using log::error;
    using log::fatal;
} // export namespace lib