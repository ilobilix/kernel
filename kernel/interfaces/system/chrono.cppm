// Copyright (C) 2024-2026  ilobilo

export module system.chrono;

import lib;
import frigg;
import std;

export namespace chrono
{
    struct clock
    {
        friend void register_clock(clock &timer);

        frg::pairing_heap_hook<clock> hook;

        private:
        std::string _name;
        std::size_t _priority;
        std::int64_t _offset;

        std::uint64_t (*_ns)();

        public:
        clock(std::string_view name, std::size_t priority, std::uint64_t (*time_ns)());

        std::string_view name() const { return _name; }
        std::size_t priority() const { return _priority; }

        std::uint64_t ns() const;
    };

    void register_clock(clock &timer);
    clock *main_clock();

    bool stall_ns(std::size_t ns);

    timespec now(clockid_t clockid = 0);
} // export namespace chrono