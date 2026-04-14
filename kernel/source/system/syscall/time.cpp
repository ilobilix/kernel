// Copyright (C) 2024-2025  ilobilo

module system.syscall.time;

import system.time;
import lib;
import cppstd;

namespace syscall::time
{
    int clock_gettime(clockid_t clockid, timespec __user *tp)
    {
        const auto now = ::time::now(clockid);
        lib::copy_to_user(tp, &now, sizeof(timespec));
        return 0;
    }
} // namespace syscall::time