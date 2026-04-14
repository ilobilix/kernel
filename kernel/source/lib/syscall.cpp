// Copyright (C) 2024-2025  ilobilo

module lib;

import system.scheduler;
import std;

namespace lib::syscall
{
    std::pair<std::size_t, std::size_t> get_ptid()
    {
        auto me = sched::this_thread();
        return { me->parent->pid, me->tid };
    }
} // namespace lib::syscall