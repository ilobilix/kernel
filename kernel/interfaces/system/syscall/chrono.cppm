// Copyright (C) 2024-2026  ilobilo

export module system.syscall.chrono;
import lib;

export namespace syscall::chrono
{
    int clock_gettime(clockid_t clockid, timespec __user *tp);
    int clock_settime(clockid_t clockid, const timespec __user *tp);
    int clock_getres(clockid_t clockid, timespec __user *res);
    int clock_nanosleep(
        const clockid_t clockid, int flags,
        const timespec __user *time, timespec __user *remain
    );
    int nanosleep(const timespec __user *time, timespec __user *remain);

    int gettimeofday(timeval __user *tv, struct timezone __user *tz);
    int settimeofday(const timeval __user *tv, const struct timezone __user *tz);

    time_t time(time_t __user *tloc);

    struct user_sigevent;
    int timer_create(clockid_t clockid, user_sigevent __user *sevp, int __user *timerid);
    int timer_settime(
        int timerid, int flags,
        const itimerspec __user *ntmr, itimerspec __user *otmr
    );
    int timer_gettime(int timerid, itimerspec __user *otmr);
    int timer_getoverrun(int timerid);
    int timer_delete(int timerid);
} // export namespace syscall::chrono
