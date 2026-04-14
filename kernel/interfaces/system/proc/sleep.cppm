// Copyright (C) 2024-2026  ilobilo

export module system.sched:sleep;

import lib;

import :thread;

export namespace sched
{
    struct sleep_entry_t
    {
        thread_t *thread;
        std::uint64_t deadline_ns;
        bool expired;

        lib::rbtree_hook<sleep_entry_t> hook;
    };

    // puts entry in sleep list
    void arm_thread_timeout(sleep_entry_t *entry, std::uint64_t ns);

    // if expired, returns false and removes entry
    bool cancel_thread_timeout(sleep_entry_t *entry);

    // sleep for nanoseconds. return remaining time if interrupted
    std::uint64_t sleep_for_ns(std::uint64_t ns);

    // put the current thread to sleep
    void sleep();

    // put the current thread to uninterruptible sleep
    void block();
} // export namespace sched
