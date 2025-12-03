// Copyright (C) 2024-2025  ilobilo

export module lib:trace;

import :log;
import std;

export namespace lib
{
    void trace(log_level prefix, std::uintptr_t fp, std::uintptr_t ip);
} // export namespace lib