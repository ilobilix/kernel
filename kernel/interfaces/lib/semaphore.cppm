// Copyright (C) 2024-2025  ilobilo

export module lib:semaphore;

import system.scheduler.base;
import std;

import :spinlock;

export namespace lib
{
    struct semaphore
    {
        private:
        spinlock<false> lock;
        std::list<std::shared_ptr<sched::thread_base>> threads;
        std::ssize_t signals;

        bool test();

        public:
        constexpr semaphore()
            : lock { }, threads {}, signals { 0 } { }

        semaphore(const semaphore &) = delete;
        semaphore(semaphore &&) = delete;

        semaphore &operator=(const semaphore &) = delete;
        semaphore &operator=(semaphore &&) = delete;

        void wait();
        bool wait_for(std::size_t ms);
        void signal(std::size_t n = 1, bool drop = false);
    };
} // export namespace lib