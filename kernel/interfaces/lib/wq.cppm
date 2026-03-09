// Copyright (C) 2024-2026  ilobilo

export module lib:wq;

import system.scheduler.base;
import std;

import :spinlock;
import :intrusive;

export namespace lib
{
    struct wait_queue;
    struct wait_queue_entry
    {
        lib::intrusive_list_hook<wait_queue_entry> hook;
        sched::thread_base *thread;
        wait_queue *queue;

        std::atomic_bool triggered;

        wait_queue_entry(sched::thread_base *thread)
            : thread { thread }, queue { nullptr }, triggered { false } { }
    };

    struct wait_queue
    {
        private:
        lib::intrusive_list<
            wait_queue_entry,
            &wait_queue_entry::hook
        > list;
        lib::spinlock lock;

        public:
        wait_queue() : list { }, lock { } { }

        void add(wait_queue_entry *entry);
        void remove(wait_queue_entry *entry);
        void wake_all(std::size_t reason = 0);
    };

} // export namespace lib
