// Copyright (C) 2024-2026  ilobilo

export module system.sched:run_queue;

import lib;
import std;

import :thread;

namespace sched
{
    constexpr auto weight0 = nice_to_weight(0);

    constexpr std::uint64_t latency_ns = 6'000'000;
    constexpr std::uint64_t min_gran_ns = 750'000;
    constexpr std::uint64_t wakeup_gran_ns = 1'000'000;
    constexpr std::uint64_t nr_latency = latency_ns / min_gran_ns;

    inline std::uint64_t period(std::size_t nr_running)
    {
        if (nr_running > nr_latency)
            return nr_running * min_gran_ns;
        return latency_ns;
    }
} // namespace sched

export namespace sched
{
    struct run_queue_t
    {
        lib::spinlock_irq lock;

        // clang-format off
        lib::rbtree<
            thread_t, &thread_t::hook,
            lib::compare<
                thread_t,
                std::uint64_t,
                &thread_t::vruntime
            >
        > queue;
        // clang-format on

        std::uint64_t total_weight;
        std::uint64_t _min_vruntime;

        std::size_t cpu_idx;
        std::uint64_t nr_running;

        thread_t *current;
        std::shared_ptr<thread_t> idle;

        std::uint64_t nr_switches;

        std::uint64_t load_update;
        std::uint64_t tick_last_ns;

        // enqueue when thread becomes runnable
        void enqueue(thread_t *thread);
        // dequeue when thread leaves runnable state
        void dequeue(thread_t *thread);

        // pick the next thread to run (least amount of vruntime)
        // nullptr if empty
        thread_t *pick_next();

        // update current thread vruntime
        // returns the runtime delta
        std::uint64_t update_current(std::uint64_t now);

        // adjust vruntime
        void adjust(thread_t *thread, bool initial);

        // check if current should be preempted with thread
        bool check_preempt_wakeup(thread_t *thread);

        // calculate fair timeslice for a thread
        std::uint64_t calc_timeslice(std::uint64_t weight);

        std::uint64_t calc_vruntime(
            std::uint64_t delta_ns, std::uint64_t weight, std::uint64_t inv_weight
        );

        void update_min_vruntime();
    };
} // export namespace sched
