// Copyright (C) 2024-2025  ilobilo

import drivers.output.terminal;
import drivers.output.serial;

import system.cpu;
import boot;
import arch;
import lib;
import fmt;
import std;

#if ILOBILIX_EXTRA_PANIC_MSG
namespace
{
    signed char nooo_unicode[] {
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

    [[noreturn, clang::no_sanitize("undefined")]]
    void vpanic(std::string_view fmt, fmt::format_args args, cpu::registers *regs, std::source_location location)
    {
        arch::halt_others();

        static std::atomic_bool panicking = false;
        if (panicking)
        {
            lib::fatal("one panic was more than enough already and now you want a second one?");
            goto exit;
        }
        panicking = true;

        lib::log::unsafe::unlock();

        lib::println("");
#if ILOBILIX_EXTRA_PANIC_MSG
#  if !ILOBILIX_MAX_UACPI_POINTS
        if (auto ctx = output::term::main())
            output::term::write(ctx, nooo_ascii);
#  endif
        for (auto chr : nooo_unicode)
            output::serial::printc(chr);
        lib::println("");
#endif

        lib::fatal("kernel panicked with the following message:");
        lib::fatal(fmt, args);
        lib::fatal("at {}:{}:{}: {}", location.file_name(), location.line(), location.column(), location.function_name());

        if (regs)
        {
            arch::dump_regs(regs, cpu::extra_regs::read(), lib::log_level::fatal);
            lib::trace(lib::log_level::fatal, regs->fp(), regs->ip());
        }
        else lib::trace(lib::log_level::fatal, 0, 0);

        exit:
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