// Copyright (C) 2024-2026  ilobilo

module system.sched;

namespace sched
{
    void wait_queue_t::prepare_wait(wait_queue_entry_t *entry, thread_state state)
    {
        auto locked = entries.lock();
        locked->push_back(entry);
        entry->thread->state = state;
    }

    void wait_queue_t::finish_wait(wait_queue_entry_t *entry)
    {
        auto locked = entries.lock();
        locked->remove(entry);
    }

    void wait_queue_t::wake_one()
    {
        auto locked = entries.lock();
        if (locked->empty())
            return;
        auto entry = locked->pop_front();
        wake_up(entry->thread, true);
    }

    void wait_queue_t::wake_all()
    {
        auto locked = entries.lock();
        while (!locked->empty())
        {
            auto entry = locked->pop_front();
            wake_up(entry->thread, true);
        }
    }

    void wait_queue_t::wake_exclusive()
    {
        auto locked = entries.lock();
        auto it = locked->begin();
        while (it != locked->end())
        {
            auto entry = (it++).value();
            locked->remove(entry);

            const bool exclusive = entry->exclusive;
            wake_up(entry->thread, true);

            if (exclusive)
                break;
        }
    }
} // namespace sched
