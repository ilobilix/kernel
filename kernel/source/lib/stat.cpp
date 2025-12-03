// Copyright (C) 2024-2025  ilobilo

module lib;

import system.chrono;
import std;

void stat::update_time(std::uint8_t flags)
{
    if (flags & time::access)
        st_atim = ::chrono::now();
    if (flags & time::modify)
        st_mtim = ::chrono::now();
    if (flags & time::status)
        st_ctim = ::chrono::now();
}