// Copyright (C) 2024-2026  ilobilo

module system.syscall.chrono;

import system.chrono;
import system.sched;
import magic_enum;
import std;

namespace syscall::chrono
{
    using namespace ::chrono;

    int clock_gettime(clockid_t clockid, timespec __user *tp)
    {
        const auto cur = now(static_cast<chrono::type>(clockid));
        if (!lib::copy_to_user(tp, &cur, sizeof(timespec)))
            return -EFAULT;
        return 0;
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
        const clockid_t clockid, int flags, const timespec __user *time, timespec __user *remain
    )
    {
        constexpr int abstime = 1;
        if ((flags & ~abstime) != 0)
            return -EINVAL;

        timespec ktime;
        if (!lib::copy_from_user(&ktime, time, sizeof(timespec)))
            return -EFAULT;

        if (ktime.tv_nsec < 0 || ktime.tv_nsec >= 1'000'000'000l)
            return -EINVAL;

        std::size_t ns = 0;
        if (flags & abstime)
        {
            const auto now = chrono::now(static_cast<chrono::type>(clockid));
            if (now.to_ns() == 0 || ktime < now)
                return -EINVAL;
            ns = (now - ktime).to_ns();
        }
        else
            ns = ktime.to_ns();

        if (ns == 0)
        {
            timespec tmp { 0 };
            if (remain && !lib::copy_to_user(remain, &tmp, sizeof(timespec)))
                return -EFAULT;
            return 0;
        }

        const auto timer = chrono::main_timer();
        const auto deadline = timer->ns() + ns;

        while (true)
        {
            const auto now = timer->ns();
            if (now >= deadline)
            {
                timespec tmp { 0 };
                if (remain && !lib::copy_to_user(remain, &tmp, sizeof(timespec)))
                    return -EFAULT;
                return 0;
            }

            const auto rns = sched::sleep_for_ns(deadline - now);
            if (rns == 0)
            {
                timespec tmp { 0 };
                if (remain && !lib::copy_to_user(remain, &tmp, sizeof(timespec)))
                    return -EFAULT;
                return 0;
            }

            if (sched::consume_pending_stops())
                continue;

            timespec tmp { rns };
            if (remain && !lib::copy_to_user(remain, &tmp, sizeof(timespec)))
                return -EFAULT;
            return -EINTR;
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
            timezone ktz { .tz_minuteswest = 0, .tz_dsttime = 0 };
            if (!lib::copy_to_user(tz, &ktz, sizeof(timezone)))
                return -EFAULT;
        }
        return 0;
    }

    int settimeofday(const timeval __user *tv, const timezone __user *tz)
    {
        // TODO
        lib::unused(tv, tz);
        return 0;
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
