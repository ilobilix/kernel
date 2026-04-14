// Copyright (C) 2024-2025  ilobilo

export module lib:rwlock;

import :mutex;
import cppstd;

export namespace lib
{
    // TODO
    struct rwlock
    {
        private:

        public:
        constexpr rwlock() { }

        rwlock(const rwlock &) = delete;
        rwlock(rwlock &&) = delete;

        rwlock &operator=(const rwlock &) = delete;
        rwlock &operator=(rwlock &&) = delete;

        void read_lock()
        {
        }

        void write_lock()
        {
        }

        void read_unlock()
        {
        }

        void write_unlock()
        {
        }

        bool is_read_locked()
        {
            return false;
        }

        bool is_write_locked()
        {
            return false;
        }
    };

    struct rwmutex
    {
        private:
        std::size_t counter;
        mutex readers;
        mutex global;

        public:
        constexpr rwmutex()
            : counter { 0 }, readers { }, global { } { }

        rwmutex(const rwmutex &) = delete;
        rwmutex(rwmutex &&) = delete;

        rwmutex &operator=(const rwmutex &) = delete;
        rwmutex &operator=(rwmutex &&) = delete;

        void read_lock()
        {
            readers.lock();
            if (++counter == 1)
                global.lock();
            readers.unlock();
        }

        void write_lock()
        {
            global.lock();
        }

        void read_unlock()
        {
            if (global.is_locked() == false || counter == 0)
                return;

            readers.lock();
            if (--counter == 0)
                global.unlock();
            readers.unlock();
        }

        void write_unlock()
        {
            global.unlock();
        }

        bool is_read_locked()
        {
            return global.is_locked() && counter > 0;
        }

        bool is_write_locked()
        {
            return global.is_locked() && counter == 0;
        }
    };
} // export namespace lib