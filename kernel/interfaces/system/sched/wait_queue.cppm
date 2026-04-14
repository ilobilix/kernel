// Copyright (C) 2024-2026  ilobilo

export module system.sched:wait_queue;

import lib;
import std;

import :thread;

export namespace sched
{
    thread_t *current_thread();
    void schedule();

    struct wait_queue_entry_t
    {
        thread_t *thread;
        bool exclusive;

        lib::intrusive_list_hook<wait_queue_entry_t> hook;
    };

    struct wait_queue_t
    {
        lib::locker<
            lib::intrusive_list<
                wait_queue_entry_t,
                &wait_queue_entry_t::hook
            >, lib::spinlock
        > entries;

        wait_queue_t();

        void prepare_wait(wait_queue_entry_t *entry, thread_state state);
        void finish_wait(wait_queue_entry_t *entry);

        // returns true if interrupted
        template<typename Pred>
        bool wait(Pred &&pred)
        {
            if (pred())
                return false;

            wait_queue_entry_t entry {
                .thread = current_thread(),
                .exclusive = false,
                .hook = { }
            };

            while (true)
            {
                prepare_wait(&entry, thread_state::sleeping);
                if (pred())
                {
                    finish_wait(&entry);
                    return false;
                }

                if (signal_pending(entry.thread))
                {
                    finish_wait(&entry);
                    return true;
                }

                schedule();
            }
        }

        template<typename Pred>
        void wait_uninterruptible(Pred &&pred)
        {
            if (pred())
                return;

            wait_queue_entry_t entry {
                .thread = current_thread(),
                .exclusive = false,
                .hook = { }
            };

            while (true)
            {
                prepare_wait(&entry, thread_state::blocked);
                if (pred())
                {
                    finish_wait(&entry);
                    return;
                }

                schedule();
            }
        }

        void wake_one();
        void wake_all();
        void wake_exclusive();

        // check if thread has pending signals
        static bool signal_pending(const thread_t *thread);
    };
} // export namespace sched
