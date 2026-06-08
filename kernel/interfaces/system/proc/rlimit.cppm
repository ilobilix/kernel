// Copyright (C) 2024-2026  ilobilo

export module system.sched:rlimit;

import lib;
import std;

export namespace sched
{
    enum rlimit_resource
    {
        rlimit_cpu = 0,
        rlimit_fsize = 1,
        rlimit_data = 2,
        rlimit_stack = 3,
        rlimit_core = 4,
        rlimit_rss = 5,
        rlimit_nproc = 6,
        rlimit_nofile = 7,
        rlimit_memlock = 8,
        rlimit_as = 9,
        rlimit_locks = 10,
        rlimit_sigpending = 11,
        rlimit_msgqueue = 12,
        rlimit_nice = 13,
        rlimit_rtprio = 14,
        rlimit_rttime = 15,
        rlimit_nlimits = 16
    };

    struct rlimit
    {
        rlim_t cur;
        rlim_t max;
    };

    struct rlimits_t
    {
        rlimit limits[rlimit_nlimits] {
            [rlimit_cpu] = { rlim_inf, rlim_inf },
            [rlimit_fsize] = { rlim_inf, rlim_inf },
            [rlimit_data] = { rlim_inf, rlim_inf },
            [rlimit_stack] = { 8 * 1024 * 1024, rlim_inf }, // _STK_LIM
            [rlimit_core] = { 0, rlim_inf },
            [rlimit_rss] = { rlim_inf, rlim_inf },
            [rlimit_nproc] = { 0, 0 },
            [rlimit_nofile] = { 1024, 4096 }, // INR_OPEN_CUR, INR_OPEN_MAX
            [rlimit_memlock] = { 8 * 1024 * 1024, 8 * 1024 * 1024 }, // MLOCK_LIMIT
            [rlimit_as] = { rlim_inf, rlim_inf },
            [rlimit_locks] = { rlim_inf, rlim_inf },
            [rlimit_sigpending] = { 0, 0 },
            [rlimit_msgqueue] = { 819200, 819200 }, // MQ_BYTES_MAX
            [rlimit_nice] = { 0, 0 },
            [rlimit_rtprio] = { 0, 0 },
            [rlimit_rttime] = { rlim_inf, rlim_inf }
        };
        mutable lib::spinlock lock;

        constexpr rlimits_t() = default;

        rlimit get(int resource) const
        {
            const std::unique_lock _ { lock };
            return limits[resource];
        }

        void set(int resource, rlimit val)
        {
            const std::unique_lock _ { lock };
            limits[resource] = val;
        }

        std::shared_ptr<rlimits_t> clone() const
        {
            auto cloned = std::make_shared<rlimits_t>();
            const std::unique_lock _ { lock };
            std::memcpy(cloned->limits, limits, sizeof(limits));
            return cloned;
        }
    };
} // export namespace sched
