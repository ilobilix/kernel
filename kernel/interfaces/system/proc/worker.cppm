// Copyright (C) 2024-2026  ilobilo

export module system.sched:worker;

import system.sched.wait_queue;
import lib;
import std;

import :nice;
import :thread;

export namespace sched
{
    class irq_worker_t
    {
        private:
        std::function<void ()> _drain;
        std::string _name;
        std::size_t _cpu;
        nice_t _nice;

        std::weak_ptr<thread_t> _thread;
        wait_queue_t _bell;
        std::atomic_bool _ack;
        std::atomic_bool _stopping;

        static void entry(irq_worker_t *self);

        public:
        static constexpr nice_t default_worker_nice = -20;

        irq_worker_t(
            std::string_view name, std::size_t cpu,
            std::function<void ()> drain, nice_t nice = default_worker_nice
        ) : _drain { std::move(drain) }, _name { name }, _cpu { cpu }, _nice { nice },
            _ack { false }, _stopping { false } { }

        irq_worker_t(const irq_worker_t &) = delete;
        irq_worker_t &operator=(const irq_worker_t &) = delete;

        ~irq_worker_t() { stop(); }

        lib::expect<void> start();
        void stop();

        void wake() { _bell.wake_one(); }

        std::string_view name() const { return _name; }
        std::size_t cpu() const { return _cpu; }
        bool running() const { return !_thread.expired(); }
    };
} // export namespace sched
