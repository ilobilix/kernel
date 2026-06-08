// Copyright (C) 2024-2026  ilobilo

module;

#include <cerrno>

export module lib:errno;
import std;

export {
    using enum errnos;
    using errnos = errnos;
} // export
