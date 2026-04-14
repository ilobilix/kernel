// Copyright (C) 2024-2026  ilobilo

export module lib:panic;

import :string;
import system.cpu.regs;
import fmt;
import std;

namespace lib
{
    constexpr comptime_string panic_if_message { "an unfortunate occurrence, which was definitively supposed to have been avoided or precluded, has regrettably come to fruition in the present temporal reality." };
} // namespace lib

export namespace lib
{
    [[noreturn]]
    void stop_all();

    void check_if_panicking();

    [[noreturn]]
    void vpanic(
        std::size_t len, std::string_view fmt, fmt::format_args args,
        cpu::registers *regs, std::source_location location
    );

    template<comptime_string Str, bool Regs, typename ...Args>
    struct panic_base
    {
        static constexpr bool Check = !Str.is_empty();

        [[noreturn]] panic_base(
                fmt::format_string<Args...> fmt, Args &&...args,
                const std::source_location &location = std::source_location::current()
            ) requires (!Check)
        {
            const auto len = fmt::formatted_size(fmt, std::forward<Args>(args)...);
            const std::string_view view { fmt.get().data(), fmt.get().size() };
            vpanic(len, view, fmt::make_format_args(args...), nullptr, location);
        }

        [[noreturn]] panic_base(
                cpu::registers *regs, fmt::format_string<Args...> fmt, Args &&...args,
                const std::source_location &location = std::source_location::current()
            ) requires (!Check && Regs)
        {
            const auto len = fmt::formatted_size(fmt, std::forward<Args>(args)...);
            const std::string_view view { fmt.get().data(), fmt.get().size() };
            vpanic(len, view, fmt::make_format_args(args...), regs, location);
        }

        constexpr panic_base(
                bool condition, fmt::format_string<Args...> fmt, Args &&...args,
                const std::source_location &location = std::source_location::current()
            ) requires Check
        {
            if (!std::is_constant_evaluated() && condition)
            {
                const auto len = fmt::formatted_size(fmt, std::forward<Args>(args)...);
                const std::string_view view { fmt.get().data(), fmt.get().size() };
                vpanic(len, view, fmt::make_format_args(args...), nullptr, location);
            }
        }

        panic_base(
                bool condition, cpu::registers *regs, fmt::format_string<Args...> fmt, Args &&...args,
                const std::source_location &location = std::source_location::current()
            ) requires (Check && Regs)
        {
            if (condition)
            {
                const auto len = fmt::formatted_size(fmt, std::forward<Args>(args)...);
                const std::string_view view { fmt.get().data(), fmt.get().size() };
                vpanic(len, view, fmt::make_format_args(args...), regs, location);
            }
        }

        constexpr panic_base(
                bool condition,
                const std::source_location &location = std::source_location::current()
            ) requires Check
        {
            if (!std::is_constant_evaluated() && condition)
                vpanic(Str.size(), Str.value, fmt::make_format_args(), nullptr, location);
        }

        panic_base(
                bool condition, cpu::registers *regs,
                const std::source_location &location = std::source_location::current()
            ) requires (Check && Regs)
        {
            if (condition)
                vpanic(Str.size(), Str.value, fmt::make_format_args(), regs, location);
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
    panic(fmt::format_string<Args...>, Args &&...) -> panic<Args...>;
    template<typename ...Args>
    panic(cpu::registers *, fmt::format_string<Args...>, Args &&...) -> panic<Args...>;

    template<typename ...Args>
    panic_if(bool) -> panic_if<Args...>;
    template<typename ...Args>
    panic_if(bool, cpu::registers *) -> panic_if<Args...>;
    template<typename ...Args>
    panic_if(bool, fmt::format_string<Args...>, Args &&...) -> panic_if<Args...>;
    template<typename ...Args>
    panic_if(bool, cpu::registers *, fmt::format_string<Args...>, Args &&...) -> panic_if<Args...>;
} // export namespace lib
