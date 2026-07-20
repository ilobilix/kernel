// Copyright (C) 2024-2026  ilobilo

export module system.sched.mutex;

import system.sched.thread_base;
import system.sched.wait_queue;
import system.chrono;

import lib;
import std;

export namespace sched
{
    struct mutex_t
    {
        private:
        mutable lib::spinlock _lock;
        thread_base_t *_owner;
        wait_queue_t _waiters;

        static thread_base_t *current_thread();

        public:
        mutex_t() : _lock { }, _owner { nullptr }, _waiters { } { }

        mutex_t(const mutex_t &) = delete;
        mutex_t(mutex_t &&) = delete;
        mutex_t &operator=(const mutex_t &) = delete;
        mutex_t &operator=(mutex_t &&) = delete;

        void lock()
        {
            while (true)
            {
                const auto gen = _waiters.snapshot_gen();
                {
                    const std::unique_lock _ { _lock };
                    const auto thread = current_thread();

                    if (_owner == nullptr)
                    {
                        _owner = thread;
                        return;
                    }
                    else if (_owner == thread)
                        lib::panic("mutex deadlock");
                }

                _waiters.wait_unkillable_prepared(gen);
            }
        }

        [[nodiscard]]
        bool lock_interruptible()
        {
            while (true)
            {
                const auto gen = _waiters.snapshot_gen();
                {
                    const std::unique_lock _ { _lock };
                    const auto thread = current_thread();

                    if (_owner == nullptr)
                    {
                        _owner = thread;
                        return true;
                    }
                    else if (_owner == thread)
                        lib::panic("mutex deadlock");
                }

                const auto res = _waiters.wait_prepared(gen);
                if (res.interrupted || res.killed)
                    return false;
            }
        }

        bool unlock()
        {
            {
                const std::unique_lock _ { _lock };

                if (_owner != current_thread())
                    return false;

                _owner = nullptr;
            }

            _waiters.wake_all();
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
                const auto gen = _waiters.snapshot_gen();
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

                const auto res = _waiters.wait_prepared(gen, deadline - now);
                if (res.interrupted || res.killed)
                    return false;
            }
        }

        bool is_locked() const
        {
            const std::unique_lock _ { _lock };
            return _owner != nullptr;
        }
    };

    struct recursive_mutex_t
    {
        private:
        mutable lib::spinlock _lock;
        thread_base_t *_owner;
        std::size_t _depth;
        wait_queue_t _waiters;

        static thread_base_t *current_thread();

        public:
        recursive_mutex_t() : _lock { }, _owner { nullptr }, _depth { 0 }, _waiters { } { }

        recursive_mutex_t(const recursive_mutex_t &) = delete;
        recursive_mutex_t(recursive_mutex_t &&) = delete;
        recursive_mutex_t &operator=(const recursive_mutex_t &) = delete;
        recursive_mutex_t &operator=(recursive_mutex_t &&) = delete;

        void lock()
        {
            while (true)
            {
                const auto gen = _waiters.snapshot_gen();
                {
                    const std::unique_lock _ { _lock };
                    const auto thread = current_thread();
                    if (_owner == nullptr)
                    {
                        lib::bug_on(_depth != 0);
                        _owner = thread;
                        _depth++;
                        return;
                    }
                    else if (_owner == thread)
                    {
                        lib::bug_on(_depth == 0);
                        _depth++;
                        return;
                    }
                }

                _waiters.wait_unkillable_prepared(gen);
            }
        }

        [[nodiscard]]
        bool lock_interruptible()
        {
            while (true)
            {
                const auto gen = _waiters.snapshot_gen();
                {
                    const std::unique_lock _ { _lock };
                    const auto thread = current_thread();
                    if (_owner == nullptr)
                    {
                        lib::bug_on(_depth != 0);
                        _owner = thread;
                        _depth++;
                        return true;
                    }
                    else if (_owner == thread)
                    {
                        lib::bug_on(_depth == 0);
                        _depth++;
                        return true;
                    }
                }

                const auto res = _waiters.wait_prepared(gen);
                if (res.interrupted || res.killed)
                    return false;
            }
        }

        bool unlock()
        {
            bool wake = false;
            {
                const std::unique_lock _ { _lock };

                if (_owner != current_thread())
                    return false;

                if (--_depth == 0)
                {
                    _owner = nullptr;
                    wake = true;
                }
            }

            if (wake)
                _waiters.wake_all();
            return true;
        }

        bool try_lock()
        {
            const std::unique_lock _ { _lock };
            const auto thread = current_thread();

            if (_owner != nullptr && _owner != thread)
                return false;

            if (_owner == nullptr)
            {
                lib::bug_on(_depth != 0);
                _owner = thread;
                _depth++;
                return true;
            }
            else if (_owner == thread)
            {
                lib::bug_on(_depth == 0);
                _depth++;
                return true;
            }

            return false;
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
                const auto gen = _waiters.snapshot_gen();
                {
                    const std::unique_lock _ { _lock };
                    const auto thread = current_thread();

                    if (_owner == nullptr)
                    {
                        lib::bug_on(_depth != 0);
                        _owner = thread;
                        _depth++;
                        return true;
                    }
                    else if (_owner == thread)
                    {
                        lib::bug_on(_depth == 0);
                        _depth++;
                        return true;
                    }
                }

                const auto now = timer->ns();
                if (now >= deadline)
                    return false;

                const auto res = _waiters.wait_prepared(gen, deadline - now);
                if (res.interrupted || res.killed)
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
