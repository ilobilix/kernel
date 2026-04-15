// Copyright (C) 2024-2026  ilobilo

module system.syscall.chrono;

import system.chrono;
import system.sched;
import lib;
import std;

namespace syscall::chrono
{
    using namespace ::chrono;

    int clock_gettime(clockid_t clockid, timespec __user *tp)
    {
        const auto cur = now(static_cast<chrono::type>(clockid));
        if (!lib::copy_to_user(tp, &cur, sizeof(timespec)))
            return (errno = EFAULT, -1);
        return 0;
    }

    int clock_nanosleep(
        const clockid_t clockid, int flags,
        const timespec __user *time, timespec __user *remain
    )
    {
        constexpr int abstime = 1;
        if ((flags & ~abstime) != 0)
            return (errno = EINVAL, -1);

        timespec ktime;
        if (!lib::copy_from_user(&ktime, time, sizeof(timespec)))
            return (errno = EFAULT, -1);

        if (ktime.tv_nsec < 0 || ktime.tv_nsec >= 1'000'000'000l)
            return (errno = EINVAL, -1);

        std::size_t ns = 0;
        if (flags & abstime)
        {
            const auto now = chrono::now(static_cast<chrono::type>(clockid));
            if (now.to_ns() == 0 || ktime < now)
                return (errno = EINVAL, -1);
            ns = (now - ktime).to_ns();
        }
        else ns = ktime.to_ns();

        if (ns == 0)
        {
            timespec tmp { 0 };
            if (remain && !lib::copy_to_user(remain, &tmp, sizeof(timespec)))
                return (errno = EFAULT, -1);
            return 0;
        }

        if (const auto rns = sched::sleep_for_ns(ns))
        {
            timespec tmp { rns };
            if (remain && !lib::copy_to_user(remain, &tmp, sizeof(timespec)))
                return (errno = EFAULT, -1);
            return (errno = EINTR, -1);
        }

        timespec tmp { 0 };
        if (remain && !lib::copy_to_user(remain, &tmp, sizeof(timespec)))
            return (errno = EFAULT, -1);
        return 0;
    }

    int gettimeofday(timeval __user *tv, timezone __user *tz)
    {
        const auto cur = now(chrono::realtime).to_timeval();
        if (!lib::copy_to_user(tv, &cur, sizeof(timeval)))
            return (errno = EFAULT, -1);

        if (tz != nullptr)
        {
            timezone ktz
            {
                .tz_minuteswest = 0,
                .tz_dsttime = 0
            };
            if (!lib::copy_to_user(tz, &ktz, sizeof(timezone)))
                return (errno = EFAULT, -1);
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
                return (errno = EFAULT, -1);
        }
        return seconds;
    }
} // namespace syscall::chrono
