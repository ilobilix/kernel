// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.chrono;

namespace sched
{
    namespace
    {
        enum timer_sig : std::uint8_t
        {
            timer_sig_vtalrm = 1 << 0,
            timer_sig_prof = 1 << 1
        };

        lib::locker<
            lib::intrusive_list<
                process_t,
                &process_t::timer_sig_hook
            >, lib::spinlock_irq
        > timer_sig_queue;

        wait_queue_t timer_sig_bell;

        void queue_timer_signal(process_t *proc, std::uint8_t bit)
        {
            {
                auto locked = timer_sig_queue.lock();
                proc->pending_timer_sigs.fetch_or(bit, std::memory_order_relaxed);
                if (!proc->timer_sig_self)
                {
                    proc->timer_sig_self = proc->shared_from_this();
                    locked->push_back(proc);
                }
            }
            timer_sig_bell.wake_one();
        }

        void deliver_timer_signal(process_t *proc, int signo)
        {
            const siginfo_t info {
                .signo = signo,
                .code = si_kernel,
                .err = 0,
                .pid = 0,
                .uid = 0,
                .status = 0,
                .addr = 0,
                .value = 0,
            };
            send_signal(proc, info);
        }

        [[noreturn]] void timer_sig_worker()
        {
            while (true)
            {
                sched::gen_t gen;
                while (true)
                {
                    std::shared_ptr<process_t> proc;
                    std::uint8_t bits;
                    {
                        auto locked = timer_sig_queue.lock();
                        if (locked->empty())
                        {
                            gen = timer_sig_bell.snapshot_gen();
                            break;
                        }

                        auto *front = locked->pop_front();
                        proc = std::move(front->timer_sig_self);
                        bits = front->pending_timer_sigs.exchange(0, std::memory_order_acq_rel);
                    }

                    if (bits & timer_sig_vtalrm)
                        deliver_timer_signal(proc.get(), sigvtalrm);
                    if (bits & timer_sig_prof)
                        deliver_timer_signal(proc.get(), sigprof);
                }
                timer_sig_bell.wait_prepared(gen);
            }
        }

        lib::initgraph::task timer_sig_task
        {
            "sched.timer-signals.init",
            lib::initgraph::presched_init_engine,
            lib::initgraph::require { pid0_created_stage() },
            [] {
                spawn(timer_sig_worker);
            }
        };

        bool consume_itimer(cpu_itimer_t &it, std::uint64_t delta_ns)
        {
            const std::unique_lock _ { it.lock };
            if (it.value_ns == 0)
                return false;

            if (delta_ns < it.value_ns)
            {
                it.value_ns -= delta_ns;
                return false;
            }

            const auto over = delta_ns - it.value_ns;
            it.value_ns = it.interval_ns != 0
                ? it.interval_ns - (over % it.interval_ns) : 0;
            return true;
        }

        timer_t::state_t read_cpu_itimer(cpu_itimer_t &it)
        {
            const std::unique_lock _ { it.lock };
            return { it.value_ns, it.interval_ns };
        }

        timer_t::state_t write_cpu_itimer(
            cpu_itimer_t &it,
            std::uint64_t value_ns, std::uint64_t interval_ns
        )
        {
            const std::unique_lock _ { it.lock };
            const timer_t::state_t prev { it.value_ns, it.interval_ns };

            it.value_ns = value_ns;
            it.interval_ns = value_ns == 0 ? 0 : interval_ns;
            return prev;
        }

        std::shared_ptr<real_timer_t> get_real_timer(process_t *proc, bool create)
        {
            auto locked = proc->real_timer.lock();
            if (!*locked && create)
                *locked = std::make_shared<real_timer_t>(proc->weak_from_this());
            return *locked;
        }
    } // namespace

    struct real_timer_t : timer_t
    {
        const std::weak_ptr<process_t> weak;

        real_timer_t(std::weak_ptr<process_t> proc)
            : timer_t { chrono::monotonic }, weak { std::move(proc) } { }

        void expired(std::uint64_t missed) override { lib::unused(missed); }

        void notify() override
        {
            if (const auto proc = weak.lock())
                deliver_timer_signal(proc.get(), sigalrm);
        }
    };

    timer_t::state_t itimer_get(process_t *proc, itimer_type which)
    {
        if (which == itimer_real)
        {
            const auto timer = get_real_timer(proc, false);
            return timer ? timer->query() : timer_t::state_t { 0, 0 };
        }
        return read_cpu_itimer(proc->cpu_itimers[which - 1]);
    }

    timer_t::state_t itimer_set(
        process_t *proc, itimer_type which,
        std::uint64_t value_ns, std::uint64_t interval_ns
    )
    {
        if (which == itimer_real)
        {
            const auto timer = get_real_timer(proc, value_ns != 0);
            if (!timer)
                return { 0, 0 };

            return value_ns == 0
                ? timer->disarm()
                : timer->arm(value_ns, interval_ns);
        }

        return write_cpu_itimer(proc->cpu_itimers[which - 1], value_ns, interval_ns);
    }

    void itimer_clear(process_t *proc)
    {
        if (proc == nullptr)
            return;

        if (auto timer = std::move(*proc->real_timer.lock()))
            timer->disarm();
    }

    void charge_cpu_itimers(process_t *proc, std::uint64_t delta_ns, bool from_user)
    {
        if (proc == nullptr || delta_ns == 0)
            return;

        if (from_user && consume_itimer(proc->cpu_itimers[itimer_virtual - 1], delta_ns))
            queue_timer_signal(proc, timer_sig_vtalrm);

        if (consume_itimer(proc->cpu_itimers[itimer_prof - 1], delta_ns))
            queue_timer_signal(proc, timer_sig_prof);
    }
} // namespace sched
