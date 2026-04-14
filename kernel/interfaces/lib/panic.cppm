// Copyright (C) 2024-2025  ilobilo

export module lib:panic;

import system.cpu;
import std;

export namespace lib
{
    extern "C++" [[noreturn]]
    void stop_all();

    extern "C++" [[noreturn]]
    void vpanic(std::string_view fmt, std::format_args args, cpu::registers *regs, std::source_location location);

    template<typename ...Args>
    struct panic
    {
        [[noreturn]]
        panic(std::string_view fmt, Args &&...args, const std::source_location &location = std::source_location::current())
        {
            vpanic(fmt, std::make_format_args(args...), nullptr, location);
        }

        [[noreturn]]
        panic(cpu::registers *regs, std::string_view fmt, Args &&...args, const std::source_location &location = std::source_location::current())
        {
            vpanic(fmt, std::make_format_args(args...), regs, location);
        }
    };

    template<typename ...Args>
    panic(std::string_view, Args &&...) -> panic<Args...>;

    template<typename ...Args>
    panic(cpu::registers *regs, std::string_view, Args &&...) -> panic<Args...>;
} // export namespace lib