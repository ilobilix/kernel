// Copyright (C) 2024-2025  ilobilo

module system.syscall.proc;

import system.scheduler;
import lib;

namespace syscall::proc
{
    pid_t gettid()
    {
        return sched::this_thread()->tid;
    }
} // namespace syscall::proc
