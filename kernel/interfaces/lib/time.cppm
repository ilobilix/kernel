// Copyright (C) 2024-2025  ilobilo

export module lib:time;

import :math;
import :types;
import std;

export
{
    struct timeval
    {
        time_t tv_sec;
        suseconds_t tv_usec;
    };

    struct timezone
    {
        int	tz_minuteswest;
        int	tz_dsttime;
    };

    struct timespec
    {
        time_t tv_sec;
        long tv_nsec;

        constexpr timespec() : tv_sec { 0 }, tv_nsec { 0 } { }
        constexpr timespec(timeval tv)
            : tv_sec { tv.tv_sec }, tv_nsec { static_cast<long>(tv.tv_usec) * 1'000 } { }

        constexpr timespec(time_t sec, long nsec)
            : tv_sec { sec }, tv_nsec { nsec } { }

        constexpr timespec(std::uint64_t ns)
            : tv_sec { static_cast<time_t>(ns / 1'000'000'000) },
              tv_nsec { static_cast<long>(ns % 1'000'000'000) } { }

        constexpr timespec(const timespec &other) = default;
        constexpr timespec(timespec &&other) = default;

        constexpr timespec &operator=(const timespec &other)= default;
        constexpr timespec &operator=(timespec &&other)= default;

        constexpr timespec &operator+=(const timespec &other)
        {
            tv_sec += other.tv_sec;
            tv_nsec += other.tv_nsec;
            if (tv_nsec >= 1'000'000'000)
            {
                tv_sec += tv_nsec / 1'000'000'000;
                tv_nsec = tv_nsec % 1'000'000'000;
            }
            return *this;
        }

        constexpr timespec &operator-=(const timespec &other)
        {
            tv_sec -= other.tv_sec;
            tv_nsec -= other.tv_nsec;
            if (tv_nsec < 0)
            {
                tv_sec -= 1 + (-tv_nsec) / 1'000'000'000;
                tv_nsec = 1'000'000'000 - ((-tv_nsec) % 1'000'000'000);
            }
            return *this;
        }

        constexpr timespec operator+(const timespec &other) const
        {
            timespec result = *this;
            result += other;
            return result;
        }

        constexpr timespec operator-(const timespec &other) const
        {
            timespec result = *this;
            result -= other;
            return result;
        }

        constexpr auto operator<=>(const timespec &other) const
        {
            if (tv_sec != other.tv_sec)
                return tv_sec <=> other.tv_sec;
            return tv_nsec <=> other.tv_nsec;
        }

        constexpr long to_ns() const
        {
            return tv_sec * 1'000'000'000 + tv_nsec;
        }

        constexpr timeval to_timeval() const
        {
            return timeval {
                .tv_sec = tv_sec,
                .tv_usec = static_cast<suseconds_t>(
                    lib::div_roundup(
                        static_cast<std::uint64_t>(tv_nsec), 1'000
                    )
                )
            };
        }
    };
} // export