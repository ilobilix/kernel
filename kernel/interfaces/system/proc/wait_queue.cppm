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
        std::atomic_size_t generation;

        bool try_dec_pending();

        public:
        wait_queue_t() : entries { }, lock { }, pending { 0 }, generation { 0 } { }

        void add_entry(wait_queue_entry_t &entry);
        void remove_entry(wait_queue_entry_t &entry);

        void unlink_atomic(
            std::atomic<wait_queue_t *> &on_queue_ref,
            std::atomic<wait_queue_entry_t *> &entry_ref
        );

        struct wait_result_t
        {
            bool interrupted;
            bool expired;
        };

        wait_result_t wait(std::uint64_t ns = 0);
        wait_result_t wait_unint(std::uint64_t ns = 0);

        std::size_t snapshot_gen() const;
        wait_result_t wait_prepared(std::size_t gen, std::uint64_t ns = 0);

        void wake_one(bool drop = false);
        void wake_all();
    };
} // export namespace sched
