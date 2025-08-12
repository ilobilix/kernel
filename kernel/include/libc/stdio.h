// Copyright (C) 2024-2025  ilobilo

#pragma once

#include <stddef.h>

#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

// for fmtlib
typedef size_t FILE;
extern FILE *stdout;
extern FILE *stderr;

int fflush(FILE *stream);

int fputc(char c, FILE *stream);
int fputs(const char *str, FILE *stream);
int fputws(const wchar_t *str, FILE *stream);

int fprintf(FILE *stream, const char *format, ...);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

#ifdef __cplusplus
} // extern "C"

namespace std
{
    // for fmtlib
    using ::FILE;

    using ::fflush;

    using ::fputc;
    using ::fputs;
    using ::fputws;

    using ::fprintf;
    using ::fwrite;

    [[gnu::noreturn]]
    void terminate() noexcept;
} // namespace std

#endif
