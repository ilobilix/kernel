// Copyright (C) 2024-2026  ilobilo

module system.sched;

import arch;

namespace sched
{
    void irq_worker_t::entry(irq_worker_t *self)
    {
        while (true)
        {
            const auto gen = self->_bell.snapshot_gen();

            if (self->_drain)
                self->_drain();

            if (self->_bell.wait_prepared(gen).killed)
            {
                self->_ack.store(true, std::memory_order_release);
                thread_exit(0);
            }
        }
    }

    lib::expect<void> irq_worker_t::start()
    {
        if (!_thread.expired())
            return std::unexpected { lib::err::already_exists };

        auto thread = create_kthread(
            reinterpret_cast<std::uintptr_t>(entry),
            reinterpret_cast<std::uintptr_t>(this),
            _nice
        );

        if (!thread)
            return std::unexpected { lib::err::out_of_memory };

        thread->comm = _name;
        thread->affinity.clear();
        thread->affinity.set(_cpu, true);

        _ack.store(false, std::memory_order_relaxed);
        _stopping.store(false, std::memory_order_relaxed);

        enqueue_new(thread.get());
        _thread = std::move(thread);

        return { };
    }

    void irq_worker_t::stop()
    {
        if (_stopping.exchange(true, std::memory_order_acq_rel))
            return;

        auto thread = _thread.lock();
        if (!thread)
            return;

        request_kill(thread.get(), 0);

        while (!_ack.load(std::memory_order_acquire))
            ::arch::pause();

        _thread.reset();
    }
} // namespace sched
