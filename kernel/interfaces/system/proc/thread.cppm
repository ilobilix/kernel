// Copyright (C) 2024-2026  ilobilo

export module system.sched:thread;

import system.sched.thread_base;

import system.cpu.local;
import magic_enum;
import lib;
import std;

import :arch;
import :nice;

export namespace sched
{
    enum class thread_state : std::uint8_t
    {
        runnable,
        running,
        sleeping,
        blocked, // uninterruptible sleep
        stopped,
        zombie,
        dead
    };

    enum class thread_flags : std::uint8_t
    {
        none = 0,
        kernel = (1 << 0),
        idle = (1 << 1),
        needs_resched = (1 << 2),
        interrupted = (1 << 3),
        signal_pending = (1 << 4)
    };

    using namespace magic_enum::bitwise_operators;

    struct process_t;
    struct thread_t : thread_base_t
    {
        // accessed from assembly
        cpu::processor *running_on;
        std::uintptr_t ustack_top;
        std::uintptr_t kstack_top;
        thread_t *self;

        std::atomic<std::ssize_t> preempt_count = 0;

        // sizes are fixed
        std::uintptr_t ustack_base;
        std::uintptr_t kstack_base;

        pid_t tid;
        process_t *proc;

        thread_state state = thread_state::runnable;
        thread_flags flags = thread_flags::none;
        int exit_code = 0;

        std::uint64_t vruntime = 0;
        std::uint64_t total_runtime = 0;
        std::uint64_t prev_runtime = 0;
        std::uint64_t sched_time = 0;

        nice_t nice;
        std::uint64_t weight;
        std::uint64_t inv_weight;

        bool in_rq = false;
        lib::rbtree_hook<thread_t> hook;

        std::atomic_bool *was_in_interrupt = nullptr;
        lib::spinlock_irq *needs_unlock = nullptr;

        arch::context ctx;
        arch::data adata;

        lib::bitmap affinity;

        std::uintptr_t clear_child_tid = 0;
        std::uintptr_t set_child_tid = 0;

        errnos err;

        inline bool is_kernel() const
        {
            return (flags & thread_flags::kernel) != thread_flags::none;
        }

        inline bool is_idle() const
        {
            return (flags & thread_flags::idle) != thread_flags::none;
        }

        inline bool needs_resched() const
        {
            return (flags & thread_flags::needs_resched) != thread_flags::none;
        }
    };
} // export namespace sched
