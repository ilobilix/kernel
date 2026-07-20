// Copyright (C) 2024-2026  ilobilo

module system.sched.mutex;

import system.sched;

namespace sched
{
    thread_base_t *mutex_t::current_thread()
    {
        return sched::current_thread();
    }

    thread_base_t *recursive_mutex_t::current_thread()
    {
        return sched::current_thread();
    }
} // namespace sched
