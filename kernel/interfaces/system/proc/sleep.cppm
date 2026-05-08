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

    struct process_t;
    struct alarm_entry_t
    {
        process_t *proc;
        std::uint64_t deadline_ns;
        std::uint64_t interval_ns;
        bool armed;
        bool expired;

        lib::rbtree_hook<alarm_entry_t> hook;
    };

    struct cpu_itimer_t
    {
        std::uint64_t value_ns = 0;
        std::uint64_t interval_ns = 0;
        lib::spinlock_irq lock;
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

    // arm process alarm and return previous remaining
    // interval_ns > 0 makes it repeat
    std::uint64_t arm_alarm(
        alarm_entry_t *entry, process_t *proc,
        std::uint64_t ns, std::uint64_t interval_ns = 0
    );

    // cancel alarm and return remaining
    std::uint64_t cancel_alarm(alarm_entry_t *entry);

    // get remaining ns until next fire and interval
    struct alarm_state_t { std::uint64_t remaining_ns; std::uint64_t interval_ns; };
    alarm_state_t alarm_state(alarm_entry_t *entry);
} // export namespace sched

namespace sched
{
    void expire_timeouts();
    void expire_alarms();

    void charge_cpu_itimers(process_t *proc, std::uint64_t delta_ns, bool from_user);
} // namespace sched
