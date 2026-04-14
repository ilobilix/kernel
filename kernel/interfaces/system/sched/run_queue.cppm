// Copyright (C) 2024-2026  ilobilo

export module system.sched:run_queue;

import lib;
import std;

import :thread;

namespace sched
{
    template<typename Type, typename MType, MType Type::*Member>
    class compare
    {
        public:
        static bool operator()(const Type &lhs, const Type &rhs)
        {
            return lhs.*Member < rhs.*Member;
        }
    };

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
    struct cfs_run_queue_t
    {
        lib::rbtree<
            entity_t, &entity_t::hook,
            compare<
                entity_t,
                std::uint64_t,
                &entity_t::vruntime
            >
        > queue;

        entity_t *current;

        std::uint64_t total_weight;
        std::uint64_t _min_vruntime;

        inline std::size_t num_entities() const
        {
            return queue.size();
        }

        cfs_run_queue_t()
            : queue { }, current { nullptr }, total_weight { 0 }, _min_vruntime { 0 } { }

        // enqueue when entity becomes runnable
        void enqueue(entity_t *entity);
        // dequeue when entity leaves runnable state
        void dequeue(entity_t *entity);

        // pick the next entity to run (least amount of vruntime)
        // nullptr if empty
        entity_t *pick_next();

        // update current entity vruntime
        void update_current(std::uint64_t now);

        // adjust vruntime
        void adjust(entity_t *entity, bool initial);

        // check if current should be preempted with entity
        bool check_preempt_wakeup(entity_t *entity);

        // calculate fair timeslice for an entity
        std::uint64_t calc_timeslice(std::uint64_t weight);

        std::uint64_t calc_vruntime(
            std::uint64_t delta_ns,
            std::uint64_t weight, std::uint64_t inv_weight
        );

        void update_min_vruntime();
    };

    struct run_queue_t
    {
        lib::spinlock_preempt lock;

        std::size_t cpu_idx;
        std::uint64_t nr_running;

        thread_t *current;
        thread_t *idle;

        cfs_run_queue_t cfs;

        std::uint64_t nr_switches;

        std::uint64_t load_update;
        std::uint64_t load_active;

        bool needs_resched;
    };
} // export namespace sched
