// Copyright (C) 2024-2026  ilobilo

module system.syscall.chrono;

import system.chrono;
import system.sched;
import magic_enum;
import std;

namespace syscall::chrono
{
    namespace
    {
        constexpr int timer_abstime = (1 << 0);

        itimerspec to_itimerspec(const sched::timer_t::state_t &state)
        {
            return {
                .interval = timespec { state.interval_ns },
                .value = timespec { state.remaining_ns }
            };
        }
    } // namespace

    struct user_sigevent
    {
        std::uintptr_t value;
        int signo;
        int notify;
        union {
            int pad[12];
            int tid;
            struct {
                std::uintptr_t function;
                std::uintptr_t attribute;
            } thread;
        };
    };
    static_assert(sizeof(user_sigevent) == 64);

    int timer_create(
        clockid_t clockid, user_sigevent __user *sevp, int __user *timerid
    )
    {
        const auto id = static_cast<::chrono::type>(clockid);
        if (!magic_enum::enum_contains(id))
            return -EINVAL;

        sched::sigevent_t ev {
            .value = 0,
            .signo = sched::sigalrm,
            .notify = sched::sigev_signal,
            .tid = 0
        };

        if (sevp != nullptr)
        {
            user_sigevent kev;
            if (!lib::copy_from_user(&kev, sevp, sizeof(kev)))
                return -EFAULT;

            ev.value = kev.value;
            ev.signo = kev.signo;
            ev.notify = kev.notify;
            ev.tid = kev.tid;
        }

        auto res = sched::ptimer_create(clockid, ev);
        if (!res)
            return -lib::map_error(res.error());

        if (!lib::copy_to_user(timerid, &*res, sizeof(int)))
        {
            lib::unused(sched::ptimer_delete(*res));
            return -EFAULT;
        }
        return 0;
    }

    int timer_settime(
        int timerid, int flags,
        const itimerspec __user *ntmr, itimerspec __user *otmr
    )
    {
        if (flags & ~timer_abstime)
            return -EINVAL;

        itimerspec knew;
        if (!lib::copy_from_user(&knew, ntmr, sizeof(knew)))
            return -EFAULT;

        if (!knew.valid())
            return -EINVAL;

        auto res = sched::ptimer_settime(timerid, flags & timer_abstime, knew);
        if (!res)
            return -lib::map_error(res.error());

        const auto kold = to_itimerspec(*res);
        if (otmr && !lib::copy_to_user(otmr, &kold, sizeof(kold)))
            return -EFAULT;
        return 0;
    }

    int timer_gettime(int timerid, itimerspec __user *otmr)
    {
        auto res = sched::ptimer_gettime(timerid);
        if (!res)
            return -lib::map_error(res.error());

        const auto kold = to_itimerspec(*res);
        if (!lib::copy_to_user(otmr, &kold, sizeof(kold)))
            return -EFAULT;
        return 0;
    }

    int timer_getoverrun(int timerid)
    {
        auto res = sched::ptimer_overrun(timerid);
        if (!res)
            return -lib::map_error(res.error());
        return *res;
    }

    int timer_delete(int timerid)
    {
        auto res = sched::ptimer_delete(timerid);
        if (!res)
            return -lib::map_error(res.error());
        return 0;
    }
} // namespace syscall::chrono
