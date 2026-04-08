// Copyright (C) 2024-2026  ilobilo

export module system.sched:work_queue;

import lib;
import std;

export namespace sched
{
    struct work_t
    {
        std::function<void ()> func;
        lib::intrusive_list_hook<work_t> hook;
    };

    void schedule_work(work_t *work);

    // scheduler work to run after delay_ns nanoseconds
    void schedule_work_after(work_t *work, std::uint64_t delay_ns);

    // wait for all pending work to complete
    void flush_work();
} // export namespace sched
