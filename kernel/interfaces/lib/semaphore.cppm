// Copyright (C) 2024-2026  ilobilo

export module lib:semaphore;

import system.sched.thread_base;
import std;

import :spinlock;
import :intrusive_list;

export namespace lib
{
    struct semaphore
    {
        private:
        struct locate
        {
            static lib::intrusive_list_hook<sched::thread_base_t> &operator()(sched::thread_base_t &x);
        };
        spinlock lock;
        lib::intrusive_list_locate<
            sched::thread_base_t, locate
        > threads;
        std::ssize_t signals;

        bool test();

        public:
        semaphore() : lock { }, threads { }, signals { 0 } { }

        semaphore(const semaphore &) = delete;
        semaphore(semaphore &&) = delete;

        semaphore &operator=(const semaphore &) = delete;
        semaphore &operator=(semaphore &&) = delete;

        bool wait();
        bool wait_for(std::size_t ms);
        void signal(bool drop = false);
        void signal_all();
    };
} // export namespace lib
