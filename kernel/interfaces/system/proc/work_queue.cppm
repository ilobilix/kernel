// Copyright (C) 2024-2026  ilobilo

export module system.sched:work_queue;

import lib;
import std;

import :sleep;

export namespace sched
{
    lib::initgraph::stage *wq_initialised_stage();

    void schedule_work(std::function<void()> func);
    void schedule_work_after_ns(std::function<void()> func, std::uint64_t ns);
} // export namespace sched
