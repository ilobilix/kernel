// Copyright (C) 2024-2026  ilobilo

module system.sched;

namespace sched
{
    wait_queue_t::wait_queue_t()
    {
        // TODO
    }

    void wait_queue_t::prepare_wait(wait_queue_entry_t *entry, thread_state state)
    {
        // TODO
    }

    void wait_queue_t::finish_wait(wait_queue_entry_t *entry)
    {
        // TODO
    }

    void wait_queue_t::wake_one()
    {
        // TODO
    }

    void wait_queue_t::wake_all()
    {
        // TODO
    }

    void wait_queue_t::wake_exclusive()
    {
        // TODO
    }

    bool wait_queue_t::signal_pending(const thread_t *thread)
    {
        // TODO
        return false;
    }
} // namespace sched
