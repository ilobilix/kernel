// Copyright (C) 2024-2025  ilobilo

import drivers.terminal;
import drivers.serial;

import system.cpu;
import boot;
import arch;
import lib;
import std;

#if ILOBILIX_EXTRA_PANIC_MSG
namespace
{
    std::int8_t nooo_unicode[] {
        #embed "../../embed/nooo.uni"
    };

#  if !ILOBILIX_MAX_UACPI_POINTS
    char nooo_ascii[] {
        #embed "../../embed/nooo.ascii"
    };
#  endif
} // namespace
#endif

namespace lib
{
    [[noreturn]]
    void stop_all()
    {
        arch::halt_others();
        arch::halt(false);
        std::unreachable();
    }

    [[noreturn]]
    void vpanic(std::string_view fmt, std::format_args args, cpu::registers *regs, std::source_location location)
    {
        arch::halt_others();

        static std::atomic_bool panicking = false;
        if (panicking)
            goto exit;
        panicking = true;

        log::unsafe::unlock();

        log::println("");
#if ILOBILIX_EXTRA_PANIC_MSG
#  if !ILOBILIX_MAX_UACPI_POINTS
        if (auto ctx = term::main())
            term::write(ctx, nooo_ascii);
#  endif
        for (auto chr : nooo_unicode)
            serial::printc(chr);
        log::println("");
#endif

        log::fatal("kernel panicked with the following message:");
        log::fatal(fmt, args);
        log::fatal("at {}:{}:{}: {}", location.file_name(), location.line(), location.column(), location.function_name());

        lib::trace(log::level::fatal, regs->fp(), regs->ip());

        if (regs)
            arch::dump_regs(regs, cpu::extra_regs::read(), log::level::fatal);

        exit:
        arch::halt(false);
        std::unreachable();
    }
} // namespace lib

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
    lib::vpanic(message, std::make_format_args(), nullptr, loc);
}