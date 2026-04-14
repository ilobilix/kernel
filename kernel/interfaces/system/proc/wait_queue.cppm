// Copyright (C) 2024-2026  ilobilo

export module system.sched:wait_queue;

import lib;
import std;

import :arch;
import :thread;

export namespace sched
{
    void yield();

    struct wait_queue_entry_t
    {
        thread_t *thread;
        bool exclusive;

        lib::intrusive_list_hook<wait_queue_entry_t> hook;

        wait_queue_entry_t(bool exclusive = false, thread_t *thread = arch::current_thread())
            : thread { thread }, exclusive { exclusive }, hook { } { }
    };

    struct wait_queue_t
    {
        lib::locker<
            lib::intrusive_list<
                wait_queue_entry_t,
                &wait_queue_entry_t::hook
            >, lib::spinlock
        > entries;

        wait_queue_t() = default;

        void prepare_wait(wait_queue_entry_t *entry, thread_state state);
        void finish_wait(wait_queue_entry_t *entry);

        inline void wait()
        {
            wait_queue_entry_t entry { };
            prepare_wait(&entry, thread_state::sleeping);
            yield();
        }

        inline void wait_unint()
        {
            wait_queue_entry_t entry { };
            prepare_wait(&entry, thread_state::blocked);
            yield();
        }

        void wake_one();
        void wake_all();
        void wake_exclusive();
    };
} // export namespace sched
