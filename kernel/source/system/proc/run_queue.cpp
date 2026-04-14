// Copyright (C) 2024-2026  ilobilo

module system.sched;

namespace sched
{
    void run_queue_t::enqueue(thread_t *thread)
    {
        lib::bug_on(thread->in_rq);
        queue.insert(thread);
        total_weight += thread->weight;
        thread->in_rq = true;
        update_min_vruntime();
    }

    void run_queue_t::dequeue(thread_t *thread)
    {
        lib::bug_on(!thread->in_rq);
        queue.remove(thread);
        total_weight -= thread->weight;
        thread->in_rq = false;
        update_min_vruntime();
    }

    thread_t *run_queue_t::pick_next()
    {
        if (queue.empty())
            return nullptr;
        return queue.first();
    }

    void run_queue_t::update_current(std::uint64_t now)
    {
        if (current == nullptr)
            return;

        const auto delta = now - current->sched_time;

        if (static_cast<std::int64_t>(delta) <= 0)
            return;

        current->sched_time = now;
        current->total_runtime += delta;

        current->vruntime += calc_vruntime(delta, current->weight, current->inv_weight);
        update_min_vruntime();
    }

    void run_queue_t::adjust(thread_t *thread, bool initial)
    {
        auto vruntime = _min_vruntime;
        if (initial)
        {
            // new threads start at min_vruntime + half a latency period
            // so they don't immediately preempt everything
            vruntime = period(queue.size() + 1) / 2;
        }
        else
        {
            if (thread->vruntime > vruntime)
                return;
            vruntime -= latency_ns / 2;
        }
        thread->vruntime = std::max(thread->vruntime, vruntime);
    }

    bool run_queue_t::check_preempt_wakeup(thread_t *thread)
    {
        if (current == nullptr)
            return true;

        const auto curr_vruntime = current->vruntime;
        const auto wake_vruntime = thread->vruntime;

        return static_cast<std::int64_t>(curr_vruntime - wake_vruntime) >
            static_cast<std::int64_t>(wakeup_gran_ns);
    }

    std::uint64_t run_queue_t::calc_timeslice(std::uint64_t weight)
    {
        const auto per = period(queue.size());
        if (total_weight == 0)
            return per;

        const auto slice = per * weight / total_weight;
        return std::max(slice, min_gran_ns);
    }

    std::uint64_t run_queue_t::calc_vruntime(
        std::uint64_t delta_ns,
        std::uint64_t weight, std::uint64_t inv_weight
    )
    {
        if (weight == weight0)
            return delta_ns;
        const auto tmp = static_cast<uint128_t>(delta_ns) * weight0 * inv_weight;
        return static_cast<std::uint64_t>(tmp >> 32);
    }

    void run_queue_t::update_min_vruntime()
    {
        auto vruntime = _min_vruntime;
        if (current)
            vruntime = current->vruntime;

        if (!queue.empty())
        {
            auto head = queue.first()->vruntime;
            if (!current)
                vruntime = head;
            else
                vruntime = std::min(vruntime, head);
        }
        _min_vruntime = std::max(_min_vruntime, vruntime);
    }
} // namespace sched
