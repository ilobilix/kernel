// Copyright (C) 2024-2026  ilobilo

module system.sched;

namespace sched
{
    void cfs_run_queue_t::enqueue(entity_t *entity)
    {
        lib::bug_on(entity->on_rq);
        queue.insert(entity);
        total_weight += entity->weight;
        entity->on_rq = true;
        update_min_vruntime();
    }

    void cfs_run_queue_t::dequeue(entity_t *entity)
    {
        lib::bug_on(!entity->on_rq);
        queue.remove(entity);
        total_weight -= entity->weight;
        entity->on_rq = false;
        update_min_vruntime();
    }

    entity_t *cfs_run_queue_t::pick_next()
    {
        if (queue.empty())
            return nullptr;
        return queue.first();
    }

    void cfs_run_queue_t::update_current(std::uint64_t now)
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

    void cfs_run_queue_t::adjust(entity_t *entity, bool initial)
    {
        auto vruntime = _min_vruntime;
        if (initial)
        {
            // new entities start at min_vruntime + half a latency period
            // so they don't immediately preempt everything
            vruntime = period(num_entities() + 1) / 2;
        }
        else
        {
            if (entity->vruntime > vruntime)
                return;
            vruntime -= latency_ns / 2;
        }
        entity->vruntime = std::max(entity->vruntime, vruntime);
    }

    bool cfs_run_queue_t::check_preempt_wakeup(entity_t *entity)
    {
        if (current == nullptr)
            return true;

        const auto curr_vruntime = current->vruntime;
        const auto wake_vruntime = entity->vruntime;

        return static_cast<std::int64_t>(curr_vruntime - wake_vruntime) >
            static_cast<std::int64_t>(wakeup_gran_ns);
    }

    std::uint64_t cfs_run_queue_t::calc_timeslice(std::uint64_t weight)
    {
        const auto per = period(num_entities());
        if (total_weight == 0)
            return per;

        const auto slice = per * weight / total_weight;
        return std::max(slice, min_gran_ns);
    }

    std::uint64_t cfs_run_queue_t::calc_vruntime(
        std::uint64_t delta_ns,
        std::uint64_t weight, std::uint64_t inv_weight
    )
    {
        if (weight == weight0)
            return delta_ns;
        const auto tmp = static_cast<uint128_t>(delta_ns) * weight0 * inv_weight;
        return static_cast<std::uint64_t>(tmp >> 32);
    }

    void cfs_run_queue_t::update_min_vruntime()
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
