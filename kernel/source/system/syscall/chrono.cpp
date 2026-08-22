// Copyright (C) 2024-2026  ilobilo

module system.syscall.chrono;

import system.chrono;
import system.sched;
import magic_enum;
import std;

namespace syscall::chrono
{
    using namespace ::chrono;

    namespace
    {
        int set_clock(chrono::type clockid, const timespec &ts)
        {
            if (!sched::capable(sched::cap_t::sys_time))
                return -EPERM;

            if (!ts.valid())
                return -EINVAL;

            if (!chrono::set_now(clockid, ts))
                return -EINVAL;
            return 0;
        }
    } // namespace

    int clock_gettime(clockid_t clockid, timespec __user *tp)
    {
        const auto cur = now(static_cast<chrono::type>(clockid));
        if (!lib::copy_to_user(tp, &cur, sizeof(timespec)))
            return -EFAULT;
        return 0;
    }

    int clock_settime(clockid_t clockid, const timespec __user *tp)
    {
        const auto id = static_cast<chrono::type>(clockid);
        if (!magic_enum::enum_contains(id))
            return -EINVAL;

        timespec kts;
        if (!lib::copy_from_user(&kts, tp, sizeof(timespec)))
            return -EFAULT;

        return set_clock(id, kts);
    }

    int clock_getres(clockid_t clockid, timespec __user *res)
    {
        const auto id = static_cast<chrono::type>(clockid);
        if (!magic_enum::enum_contains(id))
            return -EINVAL;

        if (res != nullptr)
        {
            // TODO
            timespec kres { 1 };
            if (!lib::copy_to_user(res, &kres, sizeof(timespec)))
                return -EFAULT;
        }
        return 0;
    }

    int clock_nanosleep(
        const clockid_t clockid, int flags,
        const timespec __user *time, timespec __user *remain
    )
    {
        constexpr int abstime = 1;
        if ((flags & ~abstime) != 0)
            return -EINVAL;

        timespec ktime;
        if (!lib::copy_from_user(&ktime, time, sizeof(timespec)))
            return -EFAULT;

        if (!ktime.valid())
            return -EINVAL;

        const auto finish = [remain](std::uint64_t rns, int ret) {
            const timespec tmp { rns };
            if (remain && !lib::copy_to_user(remain, &tmp, sizeof(timespec)))
                return -EFAULT;
            return ret;
        };

        const auto ns = (flags & abstime)
            ? chrono::delay_until(static_cast<chrono::type>(clockid), ktime)
            : ktime.to_ns();

        if (ns == 0)
            return finish(0, 0);

        const auto *timer = chrono::main_timer();
        const auto deadline = timer->ns() + ns;

        while (true)
        {
            const auto now = timer->ns();
            if (now >= deadline)
                return finish(0, 0);

            const auto rns = sched::sleep_for_ns(deadline - now);
            if (rns == 0)
                return finish(0, 0);

            if (sched::consume_pending_stops())
                continue;

            return finish(rns, -EINTR);
        }
    }

    int nanosleep(const timespec __user *time, timespec __user *remain)
    {
        return clock_nanosleep(static_cast<clockid_t>(chrono::monotonic), 0, time, remain);
    }

    int gettimeofday(timeval __user *tv, timezone __user *tz)
    {
        const auto cur = now(chrono::realtime).to_timeval();
        if (!lib::copy_to_user(tv, &cur, sizeof(timeval)))
            return -EFAULT;

        if (tz != nullptr)
        {
            timezone ktz { };
            if (!lib::copy_to_user(tz, &ktz, sizeof(timezone)))
                return -EFAULT;
        }
        return 0;
    }

    int settimeofday(const timeval __user *tv, const timezone __user *tz)
    {
        lib::unused(tz);
        if (tv == nullptr)
            return 0;

        timeval ktv;
        if (!lib::copy_from_user(&ktv, tv, sizeof(timeval)))
            return -EFAULT;

        if (ktv.tv_usec < 0 || ktv.tv_usec >= 1'000'000l)
            return -EINVAL;

        return set_clock(chrono::realtime, timespec::from_timeval(ktv));
    }

    time_t time(time_t __user *tloc)
    {
        const time_t seconds = now(chrono::realtime).tv_sec;
        if (tloc != nullptr)
        {
            if (!lib::copy_to_user(tloc, &seconds, sizeof(time_t)))
                return -EFAULT;
        }
        return seconds;
    }
} // namespace syscall::chrono
