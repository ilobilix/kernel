// Copyright (C) 2024-2026  ilobilo

export module lib:log;

import :spinlock;
import :barrier;
import :math;
import fmt;
import std;

namespace lib::log
{
    export enum class level : std::uint8_t
    {
        none,
#if ILOBILIX_DEBUG
        debug,
#endif
        info,
        warn,
        error,
        fatal
    };

    constexpr std::string_view prefixes[]
    {
        "",
#if ILOBILIX_DEBUG
        "[\e[90mdebug\e[0m] ",
#endif
        "[\e[92minfo\e[0m] ",
        "[\e[33mwarn\e[0m] ",
        "[\e[91merror\e[0m] ",
        "[\e[41mfatal\e[0m] "
    };

    namespace detail
    {
        void vprint(bool add_nl, level lvl, std::size_t len, std::string_view fmt, fmt::format_args args);

        template<typename ...Args>
        inline void print(bool add_nl, fmt::format_string<Args...> fmt, Args &&...args)
        {
            const auto len = fmt::formatted_size(fmt, std::forward<Args>(args)...);
            const std::string_view view { fmt.get().data(), fmt.get().size() };
            vprint(add_nl, len, view, fmt::make_format_args(args...));
        }
    } // namespace detail
} // namespace lib::log

export namespace lib::log
{
    struct logger
    {
        void (*prints)(std::string_view str);

        void (*start)();
        void (*stop)();

        logger *next;

        constexpr logger(auto prints, auto start, auto stop)
            : prints { prints }, start { start }, stop { stop }, next { nullptr } { }
    };

    void register_logger(logger *lg);

    void set_direct_print(bool _direct);
    void force_unlock();

    void wait_for_logs();

    inline void vprint(level lvl, std::size_t len, std::string_view fmt, fmt::format_args args)
    {
        detail::vprint(false, lvl, len, fmt, args);
    }

    inline void vprintln(level lvl, std::size_t len, std::string_view fmt, fmt::format_args args)
    {
        detail::vprint(true, lvl, len, fmt, args);
    }

    template<typename ...Args>
    inline void print(level lvl, fmt::format_string<Args...> fmt, Args &&...args)
    {
        const auto len = fmt::formatted_size(fmt, std::forward<Args>(args)...);
        const std::string_view view { fmt.get().data(), fmt.get().size() };
        vprint(lvl, len, view, fmt::make_format_args(args...));
    }

    template<typename ...Args>
    inline void println(level lvl, fmt::format_string<Args...> fmt, Args &&...args)
    {
        const auto len = fmt::formatted_size(fmt, std::forward<Args>(args)...);
        const std::string_view view { fmt.get().data(), fmt.get().size() };
        vprintln(lvl, len, view, fmt::make_format_args(args...));
    }

    template<typename ...Args>
    inline void print(fmt::format_string<Args...> fmt, Args &&...args)
    {
        print(level::none, fmt, std::forward<Args>(args)...);
    }

    template<typename ...Args>
    inline void println(fmt::format_string<Args...> fmt = "", Args &&...args)
    {
        println(level::none, fmt, std::forward<Args>(args)...);
    }

#if ILOBILIX_DEBUG
    inline void vdebug(std::size_t len, std::string_view fmt, fmt::format_args args)
    {
        vprintln(level::debug, len, fmt, args);
    }

    template<typename ...Args>
    inline void debug(fmt::format_string<Args...> fmt, Args &&...args)
    {
        println(level::debug, fmt, std::forward<Args>(args)...);
    }
#else
    inline void vdebug(std::size_t, std::string_view, fmt::format_args) { }

    template<typename ...Args>
    inline void debug(fmt::format_string<Args...>, Args &&...) { }
#endif

    inline void vinfo(std::size_t len, std::string_view fmt, fmt::format_args args)
    {
        vprintln(level::info, len, fmt, args);
    }

    template<typename ...Args>
    inline void info(fmt::format_string<Args...> fmt, Args &&...args)
    {
        println(level::info,  fmt, std::forward<Args>(args)...);
    }

    inline void vwarn(std::size_t len, std::string_view fmt, fmt::format_args args)
    {
        vprintln(level::warn, len, fmt, args);
    }

    template<typename ...Args>
    inline void warn(fmt::format_string<Args...> fmt, Args &&...args)
    {
        println(level::warn,  fmt, std::forward<Args>(args)...);
    }

    inline void verror(std::size_t len, std::string_view fmt, fmt::format_args args)
    {
        vprintln(level::error, len, fmt, args);
    }

    template<typename ...Args>
    inline void error(fmt::format_string<Args...> fmt, Args &&...args)
    {
        println(level::error, fmt, std::forward<Args>(args)...);
    }

    inline void vfatal(std::size_t len, std::string_view fmt, fmt::format_args args)
    {
        vprintln(level::fatal, len, fmt, args);
    }

    template<typename ...Args>
    inline void fatal(fmt::format_string<Args...> fmt, Args &&...args)
    {
        println(level::fatal, fmt, std::forward<Args>(args)...);
    }
} // export namespace lib::log

export namespace lib
{
    using log::logger;

    using log::vprint;
    using log::print;
    using log::vprintln;
    using log::println;

    using log::vdebug;
    using log::debug;
    using log::vinfo;
    using log::info;
    using log::vwarn;
    using log::warn;
    using log::verror;
    using log::error;
    using log::vfatal;
    using log::fatal;
} // export namespace lib
