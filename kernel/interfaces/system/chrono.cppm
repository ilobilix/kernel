// Copyright (C) 2024-2026  ilobilo

export module system.chrono;

import lib;
import frigg;
import std;

export namespace chrono
{
    enum type : clockid_t
    {
        realtime,
        monotonic
    };

    struct timer
    {
        friend void register_timer(timer &timer);

        frg::pairing_heap_hook<timer> hook;

        private:
        std::string _name;
        std::size_t _priority;
        std::int64_t _offset;

        std::uint64_t (*_ns)();

        public:
        timer(std::string_view name, std::size_t priority, std::uint64_t (*time_ns)());

        std::string_view name() const { return _name; }
        std::size_t priority() const { return _priority; }

        std::uint64_t ns() const;
    };

    struct rtc
    {
        std::string _name;
        std::uint64_t (*unix)();
    };

    void register_timer(timer &timer);
    timer *main_timer();

    bool stall_ns(std::size_t ns);

    void register_rtc(rtc &rtc);

    timespec now(type clockid);
} // export namespace chrono
