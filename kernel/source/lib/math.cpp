// Copyright (C) 2024-2026  ilobilo

module lib;

import boot;
import std;

namespace lib
{
    std::uintptr_t get_hhdm_offset()
    {
        return boot::get_hhdm_offset();
    }
} // namespace lib
