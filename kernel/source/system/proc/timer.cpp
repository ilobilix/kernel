// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.chrono;

namespace sched
{
    timer_t::state_t timer_t::query_locked(std::uint64_t now) const
    {
        return {
            armed && deadline_ns > now ? deadline_ns - now : 0,
            interval_ns
        };
    }

    timer_t::state_t timer_t::query() const
    {
        const auto now = chrono::main_timer()->ns();
        const std::unique_lock _ { lock };
        return query_locked(now);
    }

    timer_t::state_t timer_t::arm(std::uint64_t delay_ns, std::uint64_t interval_ns)
    {
        const auto now = chrono::main_timer()->ns();

        std::uint64_t gen = 0;
        bool queue = false;
        state_t prev;
        {
            const std::unique_lock _ { lock };
            prev = query_locked(now);

            this->interval_ns = interval_ns;
            this->deadline_ns = now + delay_ns;
            this->armed = true;

            rearmed();

            if ((queue = !pending || pending_ns > this->deadline_ns))
            {
                gen = ++generation;
                pending = true;
                pending_ns = this->deadline_ns;
            }
        }

        if (queue)
            schedule(gen, delay_ns);
        return prev;
    }

    timer_t::state_t timer_t::disarm()
    {
        const auto now = chrono::main_timer()->ns();

        const std::unique_lock _ { lock };
        const auto prev = query_locked(now);

        armed = false;
        interval_ns = 0;
        deadline_ns = 0;

        rearmed();
        return prev;
    }

    void timer_t::schedule(std::uint64_t gen, std::uint64_t delay_ns)
    {
        schedule_work_after_ns(
            [weak = std::weak_ptr<timer_t> { shared_from_this() }, gen] {
                fire(weak, gen);
            },
            delay_ns
        );
    }

    void timer_t::fire(const std::weak_ptr<timer_t> &weak, std::uint64_t gen)
    {
        const auto self = weak.lock();
        if (!self)
            return;

        std::uint64_t next = 0;
        bool queue = false;
        bool expired = false;
        {
            const std::unique_lock _ { self->lock };
            if (self->generation != gen)
                return;

            self->pending = false;

            if (self->armed)
            {
                const auto now = chrono::main_timer()->ns();
                if (now < self->deadline_ns)
                {
                    next = self->deadline_ns - now;
                    queue = true;
                }
                else
                {
                    std::uint64_t missed = 1;
                    if (self->interval_ns != 0)
                    {
                        missed += (now - self->deadline_ns) / self->interval_ns;

                        self->deadline_ns += missed * self->interval_ns;
                        next = self->deadline_ns > now ? self->deadline_ns - now : 0;
                        queue = true;
                    }
                    else
                    {
                        self->armed = false;
                        self->deadline_ns = 0;
                    }

                    self->expired(missed);
                    expired = true;
                }
            }

            if (queue)
            {
                self->pending = true;
                self->pending_ns = self->deadline_ns;
            }
        }

        if (queue)
            self->schedule(gen, next);
        if (expired)
            self->notify();
    }
} // namespace sched
