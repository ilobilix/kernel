// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.cpu.arch;

namespace sched::arch
{
    thread_t *current_thread()
    {
        return reinterpret_cast<thread_t *>(cpu::read_el1_base());
    }

    void init_core(thread_t *initial)
    {
        lib::unused(initial);
        // TODO
    }

    void init_thread(thread_t *thread, std::uintptr_t ip, std::uintptr_t arg, bool is_kernel)
    {
        lib::unused(thread, ip, arg, is_kernel);
        // TODO
    }

    void context_switch(thread_t *prev, thread_t *next)
    {
        lib::unused(prev, next);
        // TODO
    }

    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack)
    {
        // TODO
        lib::unused(ip, stack);
        std::unreachable();
    }
} // namespace sched::arch
