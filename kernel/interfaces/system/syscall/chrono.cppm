// Copyright (C) 2024-2025  ilobilo

export module system.syscall.chrono;
import lib;

export namespace syscall::chrono
{
    int clock_gettime(clockid_t clockid, timespec __user *tp);

    int gettimeofday(timeval __user *tv, struct timezone __user *tz);
    int settimeofday(const timeval __user *tv, const struct timezone __user *tz);
} // export namespace syscall::chrono