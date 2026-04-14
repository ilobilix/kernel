// Copyright (C) 2024-2026  ilobilo

module lib;

import drivers.output.terminal;
import drivers.output.serial;

import system.cpu;
import boot;
import arch;
import fmt;
import std;

namespace
{
    signed char nooo_unicode[] {
        #embed "../../embed/nooo.uni"
    };

    char nooo_ascii[] {
        #embed "../../embed/nooo.ascii"
    };

    std::atomic_bool panicking = false;
} // namespace

namespace lib
{
    [[noreturn]]
    void stop_all()
    {
        arch::halt_others();
        arch::halt(false);
        std::unreachable();
    }

    void check_if_panicking()
    {
        if (panicking.load())
        {
            arch::halt(false);
            std::unreachable();
        }
    }

    [[noreturn, clang::no_sanitize("undefined")]]
    void vpanic(std::string_view fmt, fmt::format_args args, cpu::registers *regs, std::source_location location)
    {
        if (panicking.exchange(true))
            goto end;

        arch::halt_others();
        log::unsafe::unlock();

        println("");
        if (auto ctx = output::term::main())
        {
            bool first = true;
            for (const auto seg : nooo_ascii | std::views::split('\n'))
            {
                if (!first)
                    output::term::write(ctx, "\r\n");
                first = false;
                output::term::write(ctx, std::string_view { seg });
            }
        }
        for (auto chr : nooo_unicode)
            output::serial::printc(chr);
        println("");

        fatal("kernel panicked with the following message:");
        fatal(fmt, args);
        fatal("at {}:{}:{}: {}", location.file_name(), location.line(), location.column(), location.function_name());

        if (regs)
        {
            arch::dump_regs(regs, cpu::extra_regs::read(), log_level::fatal);
            trace(log_level::fatal, regs->fp(), regs->ip());
        }
        else trace(log_level::fatal, 0, 0);

        end:
        arch::halt(false);
        std::unreachable();
    }
} // namespace lib

#if ILOBILIX_DEBUG
extern "C" [[gnu::noreturn]]
void assert_fail(const char *message, const char *file, int line, const char *func)
{
    struct custom_location : std::source_location
    {
        int _line;
        const char *_file;
        const char *_func;

        custom_location(const char *file, int line, const char *func)
            : _line { line }, _file { file }, _func { func } { }

        constexpr std::uint_least32_t line() const noexcept
        { return _line; }

        constexpr const char *file_name() const noexcept
        { return _file; }

        constexpr const char *function_name() const noexcept
        { return _func; }
    };
    const custom_location loc { file, line, func };
    lib::vpanic(message, fmt::make_format_args(), nullptr, loc);
}
#endif
