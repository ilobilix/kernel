// Copyright (C) 2024-2026  ilobilo

module system.sched;

namespace sched
{
    namespace
    {
        lib::expect<std::shared_ptr<ptimer_t>> lookup(int id)
        {
            auto *proc = current_process();
            auto locked = proc->ptimers.lock();

            const auto it = locked->find(id);
            if (it == locked->end())
                return std::unexpected { lib::err::invalid_argument };

            return it->second;
        }
    } // namespace

    struct ptimer_t : timer_t
    {
        const sigevent_t ev;
        const int id;
        const std::weak_ptr<process_t> wproc;

        int deliver_overrun = 0;
        std::atomic<int> last_overrun = 0;

        ptimer_t(clockid_t clockid, const sigevent_t &ev, int id, std::weak_ptr<process_t> wproc)
            : timer_t { static_cast<chrono::type>(clockid) }, ev { ev },
              id { id }, wproc { std::move(wproc) } { }

        void expired(std::uint64_t missed) override
        {
            const auto over = missed - 1;
            deliver_overrun = over > std::numeric_limits<int>::max()
                ? std::numeric_limits<int>::max()
                : static_cast<int>(over);
        }

        void notify() override
        {
            if (ev.notify == sigev_none)
                return;

            const auto proc = wproc.lock();
            if (!proc)
                return;

            int over;
            {
                const std::unique_lock _ { lock };
                over = deliver_overrun;
            }
            last_overrun.store(over, std::memory_order_release);

            const siginfo_t info {
                .signo = ev.signo,
                .code = si_timer,
                .err = 0,
                .pid = proc->pid,
                .uid = 0,
                .status = 0,
                .addr = 0,
                .value = ev.value,
                .timerid = id,
                .overrun = over
            };

            if (ev.notify == sigev_thread_id)
            {
                std::shared_ptr<thread_t> target;
                {
                    auto locked = proc->threads.lock();
                    if (const auto it = locked->find(ev.tid); it != locked->end())
                        target = it->second;
                }
                if (target)
                    send_signal(target.get(), info);
                return;
            }

            send_signal(proc.get(), info);
        }
    };

    lib::expect<int> ptimer_create(clockid_t clockid, const sigevent_t &ev)
    {
        switch (ev.notify)
        {
            case sigev_none:
                break;
            case sigev_signal:
            case sigev_thread_id:
                if (ev.signo < 1 || ev.signo > nsig)
                    return std::unexpected { lib::err::invalid_argument };
                break;
            default:
                return std::unexpected { lib::err::invalid_argument };
        }

        auto *proc = current_process();
        if (ev.notify == sigev_thread_id)
        {
            auto locked = proc->threads.lock();
            if (!locked->contains(ev.tid))
                return std::unexpected { lib::err::invalid_argument };
        }

        const auto id = proc->next_ptimer_id.fetch_add(1, std::memory_order_relaxed);
        (*proc->ptimers.lock())[id] = std::make_shared<ptimer_t>(
            clockid, ev, id, proc->weak_from_this()
        );
        return id;
    }

    lib::expect<void> ptimer_delete(int id)
    {
        auto *proc = current_process();
        std::shared_ptr<ptimer_t> timer;
        {
            auto locked = proc->ptimers.lock();
            const auto it = locked->find(id);
            if (it == locked->end())
                return std::unexpected { lib::err::invalid_argument };

            timer = std::move(it->second);
            locked->erase(it);
        }

        timer->disarm();
        return { };
    }

    lib::expect<timer_t::state_t> ptimer_settime(int id, bool abstime, const itimerspec &its)
    {
        auto res = lookup(id);
        if (!res)
            return std::unexpected { res.error() };

        return (*res)->settime(abstime, its);
    }

    lib::expect<timer_t::state_t> ptimer_gettime(int id)
    {
        auto res = lookup(id);
        if (!res)
            return std::unexpected { res.error() };
        return (*res)->query();
    }

    lib::expect<int> ptimer_overrun(int id)
    {
        auto res = lookup(id);
        if (!res)
            return std::unexpected { res.error() };
        return (*res)->last_overrun.load(std::memory_order_acquire);
    }

    void ptimer_clear(process_t *proc)
    {
        if (proc == nullptr)
            return;

        lib::map::flat_hash<int, std::shared_ptr<ptimer_t>> taken;
        {
            auto locked = proc->ptimers.lock();
            taken.swap(*locked);
        }

        for (auto &[id, timer] : taken)
        {
            lib::unused(id);
            timer->disarm();
        }
    }
} // namespace sched
