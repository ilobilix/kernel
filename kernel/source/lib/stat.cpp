// Copyright (C) 2024-2026  ilobilo

module lib;

import system.chrono;
import std;

void kstat::update_time(std::uint8_t flags)
{
    const auto now = ::chrono::now(chrono::realtime);
    if (flags & time::access)
        st_atim = now;
    if (flags & time::modify)
        st_mtim = now;
    if (flags & time::status)
        st_ctim = now;
    if (flags & time::birth)
        st_btim = now;
}
