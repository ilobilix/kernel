// Copyright (C) 2024  ilobilo

#include <printf/printf.h>

import lib;
import std;

extern "C"
{
    static std::mutex lock;

    int vprintf(const char *format, va_list arg)
    {
        std::unique_lock _ { lock };
        return vprintf_(format, arg);
    }

    int printf(const char *format, ...)
    {
        va_list arg;
        va_start(arg, format);

        const int ret = vprintf(format, arg);

        va_end(arg);
        return ret;
    }

    int vsprintf(char *str, const char *format, va_list arg)
    {
        return vsprintf_(str, format, arg);
    }

    int sprintf(char *str, const char *format, ...)
    {
        va_list arg;
        va_start(arg, format);

        const int ret = vsprintf(str, format, arg);

        va_end(arg);
        return ret;
    }

    int vsnprintf(char *str, std::size_t count, const char *format, va_list arg)
    {
        return vsnprintf_(str, count, format, arg);
    }

    int snprintf(char *str, std::size_t count, const char *format, ...)
    {
        va_list arg;
        va_start(arg, format);

        const int ret = vsnprintf(str, count, format, arg);

        va_end(arg);
        return ret;
    }

    int vasprintf(char **str, const char *format, va_list arg)
    {
        va_list tmpa;
        va_copy(tmpa, arg);

        int ret = vsnprintf(NULL, 0, format, tmpa);

        va_end(tmpa);
        if (ret < 0)
            return -1;

        *str = std::malloc<char *>(ret + 1);
        if (*str == nullptr)
            return -1;

        ret = vsprintf(*str, format, arg);
        return ret;
    }

    int asprintf(char **str, const char *format, ...)
    {
        va_list arg;
        va_start(arg, format);

        const int ret = vasprintf(str, format, arg);

        va_end(arg);
        return ret;
    }

    // fmtlib

    std::FILE *stdout = (std::FILE *)&stdout;
    std::FILE *stderr = (std::FILE *)&stderr;

    int fflush(std::FILE *) { return 0; }

    int fputc(char chr, std::FILE *)
    {
        log::unsafe::printc(chr);
        return chr;
    }

    int fputs(const char *str, std::FILE *)
    {
        return printf("%s", str);
    }

    int fputws(const wchar_t *, std::FILE *) { return -1; }

    int fprintf(std::FILE *, const char *format, ...)
    {
        std::unique_lock _ { lock };

        va_list arg;
        va_start(arg, format);

        const int ret = vprintf(format, arg);

        va_end(arg);
        return ret;
    }

    std::size_t fwrite(const void *ptr, std::size_t size, std::size_t nmemb, std::FILE *)
    {
        std::unique_lock _ { lock };

        const auto uptr = static_cast<const std::uint8_t *>(ptr);
        for (std::size_t i = 0; i < size * nmemb; i += size)
            log::unsafe::prints({ reinterpret_cast<const char *>(uptr + i), size });

        return nmemb;
    }
} // extern "C"