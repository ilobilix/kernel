// Copyright (C) 2024-2026  ilobilo

export module system.sched:thread;

import system.sched.thread_base;
import system.sched.wait_queue;

import system.memory.virt;
import system.cpu.local;
import system.cpu.regs;
import magic_enum;
import lib;
import std;

import :arch;
import :nice;
import :signal;

export namespace sched
{
    enum class thread_state : std::uint8_t
    {
        runnable,
        running,
        sleeping,
        blocked, // uninterruptible sleep
        stopped,
        dead
    };

    enum class thread_flags : std::uint8_t
    {
        none = 0,
        kernel = (1 << 0),
        idle = (1 << 1),
        needs_resched = (1 << 2),
        interrupted = (1 << 3),
        signal_pending = (1 << 4),
        quiesce_pending = (1 << 5)
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

        std::atomic<thread_state> state = thread_state::runnable;
        std::atomic<thread_state> prev_state = thread_state::runnable;
        std::atomic<std::uint8_t> flags = 0;
        int exit_code = 0;

        std::uint64_t vruntime = 0;
        std::uint64_t total_runtime = 0;
        std::uint64_t prev_runtime = 0;
        std::uint64_t sched_time = 0;

        nice_t nice;
        std::uint64_t weight;
        std::uint64_t inv_weight;

        void *on_rq = nullptr;
        lib::rbtree_hook<thread_t> hook;

        lib::intrusive_list_hook<thread_t> dead_hook;

        std::atomic_bool *was_in_interrupt = nullptr;
        lib::spinlock_irq *needs_unlock = nullptr;

        std::atomic<wait_queue_t *> on_wait_queue = nullptr;
        std::atomic<wait_queue_entry_t *> wait_entry = nullptr;

        std::atomic<bool> dead_listed = false;

        std::atomic<bool> on_cpu = false;
        thread_t *prev_to_release = nullptr;

        arch::context ctx;
        arch::data adata;

        std::shared_ptr<vmm::vmspace> saved_vmspace;
        cpu::registers *saved_regs;

        lib::bitmap affinity;

        std::uintptr_t clear_child_tid = 0;
        std::uintptr_t set_child_tid = 0;

        sigset_t sigmask { };
        std::optional<sigset_t> saved_sigmask;
        stack_t altstack { };

        inline void set_flag(thread_flags flag)
        {
            flags.fetch_or(static_cast<std::uint8_t>(flag), std::memory_order_acq_rel);
        }

        inline void clear_flag(thread_flags flag)
        {
            flags.fetch_and(~static_cast<std::uint8_t>(flag), std::memory_order_acq_rel);
        }

        inline bool has_flag(thread_flags flag) const
        {
            return (flags.load(std::memory_order_acquire) & static_cast<std::uint8_t>(flag)) != 0;
        }

        inline bool test_and_clear_flag(thread_flags flag)
        {
            const auto bits = static_cast<std::uint8_t>(flag);
            return (flags.fetch_and(~bits, std::memory_order_acq_rel) & bits) != 0;
        }

        inline bool is_kernel() const { return has_flag(thread_flags::kernel); }
        inline bool is_idle() const { return has_flag(thread_flags::idle); }
        inline bool needs_resched() const { return has_flag(thread_flags::needs_resched); }

        ~thread_t();
    };
} // export namespace sched
