// Copyright (C) 2024-2025  ilobilo

module system.syscall.time;

import system.time;
import lib;
import cppstd;

namespace syscall::time
{
    using namespace ::time;

    int clock_gettime(clockid_t clockid, timespec __user *tp)
    {
        const auto cur = now(clockid);
        if (!lib::copy_to_user(tp, &cur, sizeof(timespec)))
            return (errno = EFAULT, -1);
        return 0;
    }

    int gettimeofday(timeval __user *tv, timezone __user *tz)
    {
        const auto cur = now().to_timeval();
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
} // namespace syscall::time