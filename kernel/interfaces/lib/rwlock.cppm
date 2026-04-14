// Copyright (C) 2024-2025  ilobilo

export module lib:rwlock;

import :mutex;
import cppstd;

export namespace lib
{
    struct rwlock
    {
        private:
        std::size_t counter;
        mutex readers;
        mutex global;

        public:
        constexpr rwlock()
            : counter { 0 }, readers { }, global { } { }

        rwlock(const rwlock &) = delete;
        rwlock(rwlock &&) = delete;

        rwlock &operator=(const rwlock &) = delete;
        rwlock &operator=(rwlock &&) = delete;

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