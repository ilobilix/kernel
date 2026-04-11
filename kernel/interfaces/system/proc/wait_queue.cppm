// Copyright (C) 2024-2026  ilobilo

export module system.sched.wait_queue;

import system.sched.thread_base;

import lib;
import std;

export namespace sched
{
    struct wait_queue_entry_t
    {
        private:
        static thread_base_t *current_thread();

        public:
        thread_base_t *thread;
        lib::intrusive_list_hook<wait_queue_entry_t> hook;

        wait_queue_entry_t()
            : thread { current_thread() }, hook { } { }
    };

    struct wait_queue_t
    {
        private:
        lib::intrusive_list<
            wait_queue_entry_t,
            &wait_queue_entry_t::hook
        > entries;

        lib::spinlock_irq lock;
        std::atomic_size_t pending;

        public:
        wait_queue_t() : entries { }, lock { }, pending { 0 } { }

        bool wait(std::uint64_t ns = 0);
        void wait_unint(std::uint64_t ns = 0);

        void wake_one(bool drop = false);
        void wake_all();
    };
} // export namespace sched
