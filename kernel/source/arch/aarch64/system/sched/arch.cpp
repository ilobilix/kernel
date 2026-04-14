// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.cpu.arch;

namespace sched::arch
{
    thread_t *current_thread()
    {
        return reinterpret_cast<thread_t *>(cpu::read_el1_base());
    }

    void context_switch(thread_t *prev, thread_t *next);

    void init_thread(
        thread_t *thread, std::uintptr_t ip, std::uintptr_t arg,
        std::uintptr_t stack_top, bool is_kernel
    );

    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack);
} // namespace sched::arch
