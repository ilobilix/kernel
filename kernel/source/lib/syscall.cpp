// Copyright (C) 2024-2026  ilobilo

module lib;

import system.sched;
import std;

namespace lib::syscall
{
    std::pair<std::size_t, std::size_t> get_ptid()
    {
        auto thread = sched::current_thread();
        return { thread->proc->pid, thread->tid };
    }
} // namespace lib::syscall
