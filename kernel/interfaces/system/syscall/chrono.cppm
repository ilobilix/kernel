// Copyright (C) 2024-2026  ilobilo

export module system.syscall.chrono;
import lib;

export namespace syscall::chrono
{
    int clock_gettime(clockid_t clockid, timespec __user *tp);

    int gettimeofday(timeval __user *tv, struct timezone __user *tz);
    int settimeofday(const timeval __user *tv, const struct timezone __user *tz);

    time_t time(time_t __user *tloc);
} // export namespace syscall::chrono