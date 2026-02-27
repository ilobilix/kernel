// Copyright (C) 2024-2026  ilobilo

module lib;

import system.scheduler;
import arch;
import std;

namespace lib
{
    void wait_queue::add(wait_queue_entry *entry)
    {
        const bool ints = arch::int_switch_status(false);
        lock.lock();

        entry->queue = this;
        list.push_back(entry);

        lock.unlock();
        arch::int_switch(ints);
    }

    void wait_queue::remove(wait_queue_entry *entry)
    {
        const bool ints = arch::int_switch_status(false);
        lock.lock();

        if (entry->queue == this)
        {
            if (const auto it = list.find(entry); it != list.end())
                list.remove(it);
            entry->queue = nullptr;
        }

        lock.unlock();
        arch::int_switch(ints);
    }

    void wait_queue::wake_all(std::size_t reason)
    {
        const bool ints = arch::int_switch_status(false);
        lock.lock();

        auto temp_entries = std::move(list);

        lock.unlock();
        arch::int_switch(ints);

        for (auto &entry : temp_entries)
        {
            entry.queue = nullptr;
            entry.triggered.store(true, std::memory_order_relaxed);
            if (entry.thread)
            {
                const auto thread = static_cast<sched::thread *>(entry.thread);
                thread->wake_up(reason);
            }
        }
    }
} // namespace lib