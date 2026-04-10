// Copyright (C) 2024-2026  ilobilo

export module system.sched.mutex;

import system.sched.thread_base;
import system.sched.wait_queue;
import system.chrono;

import lib;
import std;

export namespace sched
{
    struct mutex
    {
        private:
        mutable lib::spinlock _lock;
        thread_base_t *_owner;
        wait_queue_t _waiters;

        static thread_base_t *current_thread();

        public:
        mutex() : _lock { }, _owner { nullptr }, _waiters { } { }

        mutex(const mutex &) = delete;
        mutex(mutex &&) = delete;
        mutex &operator=(const mutex &) = delete;
        mutex &operator=(mutex &&) = delete;

        bool lock()
        {
            while (true)
            {
                {
                    const std::unique_lock _ { _lock };
                    if (_owner == nullptr)
                    {
                        _owner = current_thread();
                        return true;
                    }
                }

                const bool interrupted = _waiters.wait();
                if (interrupted)
                    return false;
            }
        }

        bool unlock()
        {
            const std::unique_lock _ { _lock };

            if (_owner != current_thread())
                return false;

            _owner = nullptr;
            _waiters.wake_one();
            return true;
        }

        bool try_lock()
        {
            const std::unique_lock _ { _lock };

            if (_owner != nullptr)
                return false;

            _owner = current_thread();
            return true;
        }

        bool try_lock_until(std::uint64_t ns)
        {
            if (try_lock())
                return true;

            if (ns == 0)
                return false;

            const auto timer = chrono::main_timer();
            const auto deadline = timer->ns() + ns;

            while (true)
            {
                {
                    const std::unique_lock _ { _lock };
                    if (_owner == nullptr)
                    {
                        _owner = current_thread();
                        return true;
                    }
                }

                const auto now = timer->ns();
                if (now >= deadline)
                    return false;

                const bool interrupted = _waiters.wait(deadline - now);
                if (interrupted)
                    return false;
            }
        }

        bool is_locked() const
        {
            const std::unique_lock _ { _lock };
            return _owner != nullptr;
        }
    };
} // export namespace sched
