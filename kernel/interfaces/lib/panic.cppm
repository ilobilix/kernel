// Copyright (C) 2024-2025  ilobilo

export module lib:panic;

import :string;
import system.cpu;
import fmt;
import std;

namespace lib
{
    constexpr comptime_string panic_if_message { "an unfortunate occurrence, which was definitively supposed to have been avoided or precluded, has regrettably come to fruition in the present temporal reality." };
} // namespace lib

export namespace lib
{
    extern "C++" [[noreturn]]
    void stop_all();

    extern "C++" [[noreturn]]
    void vpanic(std::string_view fmt, fmt::format_args args, cpu::registers *regs, std::source_location location);

    template<comptime_string Str, bool Regs, typename ...Args>
    struct panic_base
    {
        static inline constexpr bool Check = !Str.is_empty();

        [[noreturn]] panic_base(
                std::string_view fmt, Args &&...args,
                const std::source_location &location = std::source_location::current()
            ) requires (!Check)
        {
            vpanic(fmt, fmt::make_format_args(args...), nullptr, location);
        }

        [[noreturn]] panic_base(
                cpu::registers *regs, std::string_view fmt, Args &&...args,
                const std::source_location &location = std::source_location::current()
            ) requires (!Check && Regs)
        {
            vpanic(fmt, fmt::make_format_args(args...), regs, location);
        }

        panic_base(
                bool condition, std::string_view fmt, Args &&...args,
                const std::source_location &location = std::source_location::current()
            ) requires Check
        {
            if (condition)
                vpanic(fmt, fmt::make_format_args(args...), nullptr, location);
        }

        panic_base(
                bool condition, cpu::registers *regs, std::string_view fmt, Args &&...args,
                const std::source_location &location = std::source_location::current()
            ) requires (Check && Regs)
        {
            if (condition)
                vpanic(fmt, fmt::make_format_args(args...), regs, location);
        }

        panic_base(
                bool condition,
                const std::source_location &location = std::source_location::current()
            ) requires Check
        {
            if (condition)
                vpanic(Str.value, fmt::make_format_args(), nullptr, location);
        }

        panic_base(
                bool condition, cpu::registers *regs,
                const std::source_location &location = std::source_location::current()
            ) requires (Check && Regs)
        {
            if (condition)
                vpanic(Str.value, fmt::make_format_args(), regs, location);
        }
    };

    template<typename ...Args>
    struct panic : panic_base<"", true, Args...>
    {
        using panic_base<"", true, Args...>::panic_base;
    };

    template<typename ...Args>
    struct panic_if : panic_base<panic_if_message, true, Args...>
    {
        using panic_base<panic_if_message, true, Args...>::panic_base;
    };

    template<typename ...Args>
    panic(std::string_view, Args &&...) -> panic<Args...>;
    template<typename ...Args>
    panic(cpu::registers *, std::string_view, Args &&...) -> panic<Args...>;

    template<typename ...Args>
    panic_if(bool) -> panic_if<Args...>;
    template<typename ...Args>
    panic_if(bool, cpu::registers *) -> panic_if<Args...>;
    template<typename ...Args>
    panic_if(bool, std::string_view, Args &&...) -> panic_if<Args...>;
    template<typename ...Args>
    panic_if(bool, cpu::registers *, std::string_view, Args &&...) -> panic_if<Args...>;
} // export namespace lib