// Copyright (C) 2024-2025  ilobilo

export module lib:mutex;

import :semaphore;
import :spinlock;
import std;

export namespace lib
{
    struct mutex
    {
        private:
        spinlock _lock;
        semaphore _sem;

        public:
        mutex() : _lock { }, _sem { } { }

        mutex(const mutex &) = delete;
        mutex(mutex &&) = delete;

        mutex &operator=(const mutex &) = delete;
        mutex &operator=(mutex &&) = delete;

        void lock()
        {
            if (is_locked())
                _sem.wait();
            _lock.lock();
        }

        bool unlock()
        {
            if (is_locked() == false)
                return false;

            _lock.unlock();
            _sem.signal();
            return true;
        }

        bool is_locked() const
        {
            return _lock.is_locked();
        }

        bool try_lock()
        {
            return _lock.try_lock();
        }

        bool try_lock_until(std::uint64_t ns)
        {
            if (is_locked())
                _sem.wait_for(ns / 1'000'000);
            return try_lock();
        }
    };
} // export namespace lib