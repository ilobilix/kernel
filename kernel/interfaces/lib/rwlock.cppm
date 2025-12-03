// Copyright (C) 2024-2025  ilobilo

export module lib:rwlock;

import :spinlock;
import :mutex;
import :bug_on;
import std;

export namespace lib
{
    template<lock_type Type>
    class rwlock_base
    {
        private:
        std::size_t counter;

        std::conditional_t<
            Type == lock_type::block,
            mutex, spinlock_base<lock_type::spin>
        > readers;
        std::conditional_t<
            Type == lock_type::block,
            mutex, spinlock_base<Type>
        > global;

        public:
        constexpr rwlock_base()
            : counter { 0 }, readers { }, global { } { }

        rwlock_base(const rwlock_base &) = delete;
        rwlock_base(rwlock_base &&) = delete;

        rwlock_base &operator=(const rwlock_base &) = delete;
        rwlock_base &operator=(rwlock_base &&) = delete;

        // TODO: broken. fix and improve it

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
            bug_on(!is_read_locked());

            readers.lock();
            if (--counter == 0)
                global.unlock();
            readers.unlock();
        }

        void write_unlock()
        {
            bug_on(!is_write_locked());
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

        bool is_locked()
        {
            return global.is_locked();
        }

        bool upgrade()
        {
            bug_on(!is_read_locked());

            readers.lock();
            bool is_last = (--counter == 0);
            readers.unlock();
            if (!is_last)
                global.lock();

            return true;
        }
    };

    using rwspinlock = rwlock_base<lock_type::spin>;
    using rwspinlock_irq = rwlock_base<lock_type::irq>;
    using rwspinlock_preempt = rwlock_base<lock_type::preempt>;
    using rwmutex = rwlock_base<lock_type::block>;
} // export namespace lib