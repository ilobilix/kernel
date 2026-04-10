// Copyright (C) 2024-2026  ilobilo

module system.sched.mutex;

import system.sched;

namespace sched
{
    thread_base_t *mutex::current_thread()
    {
        return sched::current_thread();
    }
} // namespace sched
