// Copyright (C) 2024-2026  ilobilo

module lib;

import drivers.output.terminal;
import drivers.output.serial;

import system.cpu.local;
import system.cpu;
import boot;
import arch;
import fmt;
import std;

namespace
{
    char nooo_ascii[] {
        #embed "../../embed/nooo.ascii"
    };

    std::atomic<bool> panicking = false;
    std::atomic<std::size_t> reported = 0;
    lib::spinlock dump;
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

    void check_if_panicking(cpu::registers *regs)
    {
        if (panicking.load())
        {
            if (cpu::local::available())
            {
                dump.lock();
                arch::dump_regs(cpu::self().unsafe_get().idx, regs, cpu::extra_regs::read());
                dump.unlock();
            }

            reported.fetch_add(1);

            arch::halt(false);
            std::unreachable();
        }
    }

    [[noreturn, clang::no_sanitize("undefined")]]
    void vpanic(
        std::size_t len, std::string_view fmt, fmt::format_args args,
        cpu::registers *regs, std::source_location location
    )
    {
        std::size_t cpu_idx = 0;
        if (panicking.exchange(true))
            goto end;

        log::set_direct_print(true);
        log::force_unlock();

        arch::halt_others();

        if (cpu::local::available())
        {
            while (reported.load() < cpu::count() - 1)
                arch::pause();

            cpu_idx = cpu::self().unsafe_get().idx;
        }

        arch::dump_regs(cpu_idx, regs, cpu::extra_regs::read());

        println("\n{}\n", nooo_ascii);
        fatal("kernel panicked with the following message:");
        vfatal(len, fmt, args);
        fatal(
            "at {}:{}:{}: {}",
            location.file_name(), location.line(),
            location.column(), location.function_name()
        );

        if (regs)
            trace(log::level::fatal, regs->fp(), regs->ip());
        else
            trace(log::level::fatal, 0, 0);

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
    lib::vpanic(std::strlen(message), message, fmt::make_format_args(), nullptr, loc);
}
#endif
