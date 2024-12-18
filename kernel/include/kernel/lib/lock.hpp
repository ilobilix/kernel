// Copyright (C) 2024  ilobilo

#pragma once

#include <atomic>
#include <cstdint>

// TODO: actual mutex

class ticket_lock
{
    private:
    std::atomic_size_t _next_ticket;
    std::atomic_size_t _serving_ticket;

    void arch_pause() const;

    public:
    constexpr ticket_lock(bool = true)
        : _next_ticket { 0 }, _serving_ticket { 0 } { }

    ticket_lock(const ticket_lock &) = delete;
    ticket_lock &operator=(const ticket_lock &) = delete;

    void lock()
    {
        auto ticket = _next_ticket.fetch_add(1, std::memory_order_relaxed);
        while (_serving_ticket.load(std::memory_order_acquire) != ticket)
            arch_pause();
    }

    void unlock()
    {
        if (is_locked() == false)
            return;

        auto current = _serving_ticket.load(std::memory_order_relaxed);
        _serving_ticket.store(current + 1, std::memory_order_release);
    }

    bool is_locked() const
    {
        auto current = _serving_ticket.load(std::memory_order_relaxed);
        auto next = _next_ticket.load(std::memory_order_relaxed);
        return current != next;
    }

    bool try_lock()
    {
        if (is_locked())
            return false;

        lock();
        return true;
    }

    bool try_lock_until(std::uint64_t ntimeout);
};
