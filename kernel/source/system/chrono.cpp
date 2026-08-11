// Copyright (C) 2024-2026  ilobilo

module system.chrono;

import drivers.fs.procfs;
import fmt;
import arch;
import boot;

namespace chrono
{
    namespace
    {
        struct higher_priority
        {
            constexpr bool operator()(const timer *lhs, const timer *rhs) const
            {
                return lhs->priority() < rhs->priority();
            }
        };

        frg::pairing_heap<
            timer,
            frg::locate_member<
                timer,
                frg::pairing_heap_hook<timer>,
                &timer::hook
            >,
            higher_priority
        > timers;
        timer *main = nullptr;
        rtc *main_rtc = nullptr;

        std::atomic<std::uint64_t> realtime_base_ns = 0;
        std::atomic_bool realtime_base_set = false;
        std::atomic<clock_set_hook *> clock_set_hooks = nullptr;
    } // namespace

    timer::timer(std::string_view name, std::size_t priority, std::uint64_t (*time_ns)())
            : _name { name }, _priority { priority }, _offset { 0 }, _ns { time_ns } { }

    std::uint64_t timer::ns() const
    {
        return _ns() - _offset;
    }

    void register_timer(timer &timer)
    {
        if (main != nullptr)
            timer._offset = timer._ns() - main->ns();
        else
            timer._offset = timer._ns();

        lib::info("chrono: registering timer '{}'", timer.name());
        timers.push(&timer);
        lib::debug("chrono: main timer is set to '{}'", (main = timers.top())->name());
    }

    timer *main_timer()
    {
        return main;
    }

    bool stall_ns(std::uint64_t ns)
    {
        if (main == nullptr)
            lib::panic("chrono: no timer available");

        const auto target = main->ns() + ns;
        while (main->ns() < target)
            arch::pause();

        return true;
    }

    void register_rtc(rtc &rtc)
    {
        if (main_rtc != nullptr)
            lib::panic("chrono: rtc already registered");

        lib::info("chrono: registering rtc source '{}'", rtc._name);
        main_rtc = &rtc;

        lib::bug_on(main == nullptr);

        const auto prev = main_rtc->unix();
        std::uint64_t unix_secs;
        std::uint64_t ns_before;
        do {
            ns_before = main->ns();
            unix_secs = main_rtc->unix();
        } while (unix_secs == prev);

        realtime_base_ns.store(
            unix_secs * 1'000'000'000ul - ns_before, std::memory_order_relaxed
        );
        realtime_base_set.store(true, std::memory_order_release);
    }

    std::uint64_t offset_ns(type clockid)
    {
        if (clockid != type::realtime)
            return 0;

        if (realtime_base_set.load(std::memory_order_acquire))
            return realtime_base_ns.load(std::memory_order_relaxed);
        return boot::time() * 1'000'000'000ul;
    }

    // TODO: realtime is ~1 second behind
    timespec now(type clockid)
    {
        if (main == nullptr)
            return { };

        return main->ns() + offset_ns(clockid);
    }

    bool set_now(type clockid, const timespec &ts)
    {
        if (clockid != type::realtime || main == nullptr)
            return false;

        realtime_base_ns.store(ts.to_ns() - main->ns(), std::memory_order_relaxed);
        realtime_base_set.store(true, std::memory_order_release);

        for (auto hook = clock_set_hooks.load(std::memory_order_acquire); hook; hook = hook->next)
            hook->func();

        return true;
    }

    std::uint64_t delay_until(type clockid, const timespec &deadline)
    {
        const auto cur = now(clockid);
        return deadline > cur ? (deadline - cur).to_ns() : 0;
    }

    void on_clock_set(clock_set_hook &hook)
    {
        if (hook.func == nullptr)
            return;

        auto head = clock_set_hooks.load(std::memory_order_relaxed);
        do {
            hook.next = head;
        } while (!clock_set_hooks.compare_exchange_weak(
            head, &hook, std::memory_order_release, std::memory_order_relaxed));
    }

    lib::initgraph::task procfs_register_task
    {
        "chrono.procfs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { fs::procfs::registered_stage() },
        [] {
            using namespace fs::procfs;
            lib::bug_on(!register_global("uptime",
                make_file_ops([](auto) {
                    // TODO: second field should be sum of idle time across all cpus
                    const auto t = now(monotonic);
                    const auto centi = (t.to_ms() / 10) % 100;
                    return fmt::format(
                        "{}.{:02} {}.{:02}\n",
                        t.tv_sec, centi, t.tv_sec, centi
                    );
                }), node_type::file, 0444
            ));
        }
    };
} // namespace chrono
