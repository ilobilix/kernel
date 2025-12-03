// Copyright (C) 2024-2025  ilobilo

module;

#include <cerrno>

export module lib:errno;
import std;

export
{
    using enum errnos;
    using errnos = errnos;

    using errno_type = errno_type;
    extern "C++" errno_type errno;
} // export