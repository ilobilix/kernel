// Copyright (C) 2024-2025  ilobilo

#include <stdarg.h>

import lib;
import std;

extern "C"
{
    // for fmtlib

    std::FILE *stdout = (std::FILE *)&stdout;
    std::FILE *stderr = (std::FILE *)&stderr;

    int fflush(std::FILE *) { return 0; }

    // called from do_report_error
    int fputc(char chr, std::FILE *)
    {
        log::unsafe::printc(chr);
        return 1;
    }

    // called from report_error
    int fputs(const char *str, std::FILE *)
    {
        log::unsafe::prints(str);
        return std::strlen(str);
    }

    int fputws(const wchar_t *, std::FILE *) { return -1; }

    // called from assert_fail
    int fprintf(std::FILE *, const char *format, ...)
    {
#ifdef NDEBUG
        lib::unused(format);
        std::abort();
#else
        va_list arg;
        va_start(arg, format);

        // const char *file, int line, const char *message
        va_arg(arg, const char *); va_arg(arg, int);
        auto message = va_arg(arg, const char *);

        log::unsafe::lock();
        {
            log::unsafe::printc('\n');
            log::unsafe::prints(message);
            log::unsafe::printc('\n');
        }
        log::unsafe::unlock();

        va_end(arg);
#endif
        return -1;
    }

    // called from do_report_error and fmt::print
    std::size_t fwrite(const void *ptr, std::size_t size, std::size_t nmemb, std::FILE *)
    {
        log::unsafe::lock();

        const auto uptr = static_cast<const std::uint8_t *>(ptr);
        for (std::size_t i = 0; i < size * nmemb; i += size)
            log::unsafe::prints({ reinterpret_cast<const char *>(uptr + i), size });

        log::unsafe::unlock();

        return nmemb;
    }
} // extern "C"