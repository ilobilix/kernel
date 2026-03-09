// Copyright (C) 2024-2026  ilobilo

module system.chrono;

import frigg;
import arch;
import boot;
import lib;
import std;

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

        std::uint64_t realtime_base_ns = 0;
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

        auto prev = main_rtc->unix();
        std::uint64_t unix_secs;
        while ((unix_secs = main_rtc->unix()) == prev)
            arch::pause();

        realtime_base_ns = unix_secs * 1'000'000'000ul - main->ns();
    }

    // TODO: realtime is ~1 second behind
    timespec now(type clockid)
    {
        if (main == nullptr)
            return { };

        if (clockid == type::monotonic)
            return main->ns();

        lib::panic_if(
            clockid != type::realtime,
            "unsupported clockid {}",
            static_cast<clockid_t>(clockid)
        );

        if (realtime_base_ns != 0)
            return realtime_base_ns + main->ns();

        return boot::time() * 1'000'000'000ul + main->ns();
    }
} // namespace chrono
