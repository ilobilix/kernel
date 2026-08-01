// Copyright (C) 2024-2026  ilobilo

module system.rcu;

import system.cpu.call;
import system.cpu;
import system.sched;
import frigg;

namespace rcu
{
    namespace
    {
        cpu_local(cpu_state_t, state);

        sched::mutex_t lock;
        frg::manual_box<cpu::batch_t<std::monostate>> batch;
        std::unique_ptr<std::uint64_t []> snapshot;
        bool initialised = false;

        struct default_domain_t final : domain_t
        {
            constexpr default_domain_t() = default;

            void lock() override { read_lock(); }
            bool try_lock() override { read_lock(); return true; }
            void unlock() override { read_unlock(); }

            void synchronise() override { rcu::synchronise(); }
            void barrier() override { rcu::barrier(); }
            void retire(head_t *head) override { queue_callback(head); }
        };
        constinit default_domain_t def_dom { };

        std::atomic<head_t *> callbacks = nullptr;
        sched::wait_queue_t callbacks_wq;

        std::atomic<std::uint64_t> queued_seq = 0;
        std::atomic<std::uint64_t> done_seq = 0;
        sched::wait_queue_t barrier_wq;

        std::atomic<sched::thread_t *> reclaimer_thread = nullptr;

        [[noreturn]] void reclaimer()
        {
            reclaimer_thread.store(sched::current_thread(), std::memory_order_release);

            while (true)
            {
                const auto gen = callbacks_wq.snapshot_gen();
                const auto seq = queued_seq.load(std::memory_order_acquire);

                auto batch = callbacks.exchange(nullptr, std::memory_order_acquire);
                if (batch == nullptr)
                {
                    callbacks_wq.wait_unkillable_prepared(gen);
                    continue;
                }

                head_t *current = nullptr;
                while (batch)
                {
                    const auto next = batch->next;
                    batch->next = current;
                    current = batch;
                    batch = next;
                }

                synchronise();

                while (current)
                {
                    const auto next = current->next;
                    current->func(current);
                    current = next;
                }

                done_seq.store(seq, std::memory_order_release);
                barrier_wq.wake_all();
            }
        }
    } // namespace

    cpu_state_t &get_state()
    {
        return state.unsafe_get();
    }

    void report_qs_slow()
    {
        auto &st = get_state();
        st.need_qs.store(false, std::memory_order_relaxed);
        st.qs_seq.fetch_add(1, std::memory_order_seq_cst);
    }

    void init_cpu()
    {
        ready.store(true, std::memory_order_release);
    }

    void synchronise()
    {
        lib::bug_on(nesting() != 0);
        lib::bug_on(!!sched::in_hard_irq());
        lib::bug_on(!!sched::is_preempt_disabled());

        if (!ready.load(std::memory_order_relaxed) || !sched::is_running() || cpu::count() <= 1)
            return;

        const std::unique_lock _ { lock };
        if (!initialised)
        {
            batch.initialize();
            snapshot = std::make_unique<std::uint64_t []>(cpu::count());
            initialised = true;
        }

        sched::preempt_disable();

        // orders caller publish before snapshots
        std::atomic_thread_fence(std::memory_order_seq_cst);

        batch->build([](std::size_t i, auto &) {
            auto &st = state.unsafe_get(cpu::local::nth_base(i));
            snapshot[i] = st.qs_seq.load(std::memory_order_acquire);
            st.need_qs.store(true, std::memory_order_relaxed);
            return true;
        });

        const bool none = batch->empty();
        if (!none)
        {
            batch->dispatch([](cpu::call_t *) {
                auto &st = get_state();
                if (st.nesting.load(std::memory_order_relaxed) == 0)
                    report_qs_slow();
                return true;
            });
        }

        sched::preempt_enable();
        if (none)
            return;

        batch->wait_until(
            [](std::size_t i) {
                if (!batch->at(i).done.load(std::memory_order_acquire))
                    return false;

                const auto idx = batch->target_of(i);
                auto &st = state.unsafe_get(cpu::local::nth_base(idx));
                return st.qs_seq.load(std::memory_order_acquire) != snapshot[idx];
            },
            "rcu grace period", { .yield = true }
        );
    }

    void queue_callback(head_t *head)
    {
        lib::bug_on(head == nullptr || head->func == nullptr);

        auto old = callbacks.load(std::memory_order_relaxed);
        do {
            head->next = old;
        } while (!callbacks.compare_exchange_weak(old, head,
            std::memory_order_release, std::memory_order_relaxed));

        queued_seq.fetch_add(1, std::memory_order_release);

        if (old == nullptr)
            callbacks_wq.wake_one();
    }

    void barrier()
    {
        lib::bug_on(sched::current_thread() == reclaimer_thread.load(std::memory_order_acquire));
        lib::bug_on(!!sched::in_hard_irq());

        const auto target = queued_seq.load(std::memory_order_acquire);
        if (done_seq.load(std::memory_order_acquire) >= target)
            return;

        lib::bug_on(!sched::is_running());

        while (done_seq.load(std::memory_order_acquire) < target)
        {
            const auto gen = barrier_wq.snapshot_gen();
            if (done_seq.load(std::memory_order_acquire) >= target)
                break;
            barrier_wq.wait_unkillable_prepared(gen);
        }
    }

    domain_t &default_domain()
    {
        return def_dom;
    }

    lib::initgraph::task reclaimer_task
    {
        "rcu.reclaimer.init",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { sched::pid0_created_stage() },
        [] {
            sched::spawn(reclaimer);
        }
    };
} // namespace rcu
