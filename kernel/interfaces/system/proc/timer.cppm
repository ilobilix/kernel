// Copyright (C) 2024-2026  ilobilo

export module system.sched:timer;

import system.chrono;
import lib;
import std;

export namespace sched
{
    // virtual and prof
    constexpr std::size_t cpu_itimer_count = 2;

    struct cpu_itimer_t
    {
        std::uint64_t value_ns = 0;
        std::uint64_t interval_ns = 0;
        lib::spinlock_irq lock;
    };

    enum itimer_type
    {
        itimer_real = 0,
        itimer_virtual = 1,
        itimer_prof = 2
    };

    struct timer_t : std::enable_shared_from_this<timer_t>
    {
        public:
        struct state_t
        {
            std::uint64_t remaining_ns; // 0 when disarmed
            std::uint64_t interval_ns;  // 0 for one shot

            constexpr itimerspec to_itimerspec() const
            {
                return {
                    .interval = interval_ns,
                    .value = remaining_ns
                };
            }
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
        const chrono::type clockid;

        timer_t(chrono::type clockid) : clockid { clockid } { }
        virtual ~timer_t() = default;

        state_t arm(std::uint64_t delay_ns, std::uint64_t interval_ns);
        state_t disarm();
        state_t query() const;

        state_t settime(bool abstime, const itimerspec &its);
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
    struct real_timer_t;

    timer_t::state_t itimer_get(process_t *proc, itimer_type which);
    timer_t::state_t itimer_set(
        process_t *proc, itimer_type which,
        std::uint64_t value_ns, std::uint64_t interval_ns
    );
    void itimer_clear(process_t *proc);

    lib::expect<int> ptimer_create(clockid_t clockid, const sigevent_t &ev);
    lib::expect<void> ptimer_delete(int id);
    lib::expect<timer_t::state_t> ptimer_settime(int id, bool abstime, const itimerspec &its);
    lib::expect<timer_t::state_t> ptimer_gettime(int id);
    lib::expect<int> ptimer_overrun(int id);

    void ptimer_clear(process_t *proc);
} // export namespace sched

namespace sched
{
    void charge_cpu_itimers(process_t *proc, std::uint64_t delta_ns, bool from_user);
} // namespace sched
