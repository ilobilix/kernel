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
        bool exclusive;

        lib::intrusive_list_hook<wait_queue_entry_t> hook;

        wait_queue_entry_t(bool exclusive = false, thread_base_t *thread = current_thread())
            : thread { thread }, exclusive { exclusive }, hook { } { }
    };

    struct wait_queue_t
    {
        private:
        lib::locker<
            lib::intrusive_list<
                wait_queue_entry_t,
                &wait_queue_entry_t::hook
            >, lib::spinlock
        > entries;

        public:
        wait_queue_t() = default;

        void prepare_wait(wait_queue_entry_t *entry, bool uninterruptible);
        void finish_wait(wait_queue_entry_t *entry);

        bool wait(std::uint64_t ns = 0);
        void wait_unint(std::uint64_t ns = 0);

        void wake_one();
        void wake_all();
        void wake_exclusive();
    };
} // export namespace sched
