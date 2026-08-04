// Copyright (C) 2024-2026  ilobilo

export module system.sched:timer;

import lib;
import std;

export namespace sched
{
    struct timer_t : std::enable_shared_from_this<timer_t>
    {
        public:
        struct state_t
        {
            std::uint64_t remaining_ns; // 0 when disarmed
            std::uint64_t interval_ns;  // 0 for one shot
        };

        private:
        std::uint64_t interval_ns = 0;
        std::uint64_t deadline_ns = 0;
        std::uint64_t generation = 0;

        std::uint64_t pending_ns = 0;
        bool pending = false;
        bool armed = false;

        state_t query_locked(std::uint64_t now) const;
        void schedule(std::uint64_t gen, std::uint64_t delay_ns);

        static void fire(const std::weak_ptr<timer_t> &weak, std::uint64_t gen);

        protected:
        virtual void expired(std::uint64_t missed) = 0;
        virtual void notify() = 0;
        virtual void rearmed() { }

        public:
        mutable lib::spinlock lock;

        virtual ~timer_t() = default;

        state_t arm(std::uint64_t delay_ns, std::uint64_t interval_ns);
        state_t disarm();
        state_t query() const;
    };

    enum sigev
    {
        sigev_signal = 0,
        sigev_none = 1,
        sigev_thread = 2,
        sigev_thread_id = 4
    };

    struct sigevent_t
    {
        std::uintptr_t value;
        int signo;
        int notify;
        pid_t tid;
    };

    struct process_t;
    struct ptimer_t;

    lib::expect<int> ptimer_create(clockid_t clockid, const sigevent_t &ev);
    lib::expect<void> ptimer_delete(int id);
    lib::expect<timer_t::state_t> ptimer_settime(int id, bool abstime, const itimerspec &its);
    lib::expect<timer_t::state_t> ptimer_gettime(int id);
    lib::expect<int> ptimer_overrun(int id);

    void ptimer_clear(process_t *proc);
} // export namespace sched
