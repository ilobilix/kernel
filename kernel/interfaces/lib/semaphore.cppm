// Copyright (C) 2024-2025  ilobilo

export module lib:semaphore;

import system.scheduler.base;
import std;

import :spinlock;
import :list;

export namespace lib
{
    struct semaphore
    {
        private:
        struct locate
        {
            lib::intrusive_list_hook<sched::thread_base> &operator()(sched::thread_base &x);
        };
        spinlock lock;
        lib::intrusive_list_locate<
            sched::thread_base, locate
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