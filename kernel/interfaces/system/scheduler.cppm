// Copyright (C) 2024-2025  ilobilo

export module system.scheduler;

import system.memory.virt;
import system.cpu;
import system.vfs;
import lib;
import std;

export namespace sched
{
    constexpr std::size_t fixed_timeslice = 6;

    enum class status
    {
        not_ready,
        ready,
        running,
        blocked,
        killed
    };

    struct process;
    struct thread : std::enable_shared_from_this<thread>
    {
        std::uintptr_t ustack_top;
        std::uintptr_t kstack_top;

        std::size_t tid;
        std::weak_ptr<process> proc;

        status status;
        bool is_user;

        cpu::registers regs;
        std::vector<std::byte *> stacks;

#if defined(__x86_64__)
        std::uintptr_t pfstack_top;

        std::uintptr_t gs_base;
        std::uintptr_t fs_base;

        std::byte *fpu;
#elif defined(__aarch64__)
#endif

        std::uintptr_t allocate_kstack();

        static std::shared_ptr<thread> create(std::shared_ptr<process> parent, std::uintptr_t ip);

        thread() = default;
        ~thread();
    };

    struct process
    {
        static inline constexpr std::uintptr_t initial_stck_top = 0x70000000000;

        std::size_t pid;
        // std::size_t pgid;
        // std::size_t sid;

        gid_t gid = 0, sgid = 0, egid = 0;
        uid_t uid = 0, suid = 0, euid = 0;

        std::shared_ptr<vmm::pagemap> pagemap;

        std::shared_ptr<vfs::node> root;
        std::shared_ptr<vfs::node> cwd;
        mode_t umask = static_cast<mode_t>(fmode::s_iwgrp | fmode::s_iwoth);
        // TODO: fd table

        lib::spinlock<false> lock;

        std::weak_ptr<process> parent;
        lib::map::flat_hash<std::size_t, std::shared_ptr<process>> children;
        lib::map::flat_hash<std::size_t, std::shared_ptr<thread>> threads;

        std::atomic<std::size_t> next_tid = 1;
        std::uintptr_t next_stack_top = initial_stck_top;

        static std::shared_ptr<process> create(std::shared_ptr<process> parent, std::shared_ptr<vmm::pagemap> pagemap);

        process() = default;
        ~process();
    };

    struct percpu
    {
        lib::spinlock<true> lock;

        std::list<std::shared_ptr<thread>> queue;
        std::shared_ptr<thread> running_thread;

        std::shared_ptr<process> idle_proc;
        std::shared_ptr<thread> idle_thread;
    };

    bool initialised = false;

    std::size_t allocate_cpu();
    void enqueue(std::shared_ptr<thread> thread, std::size_t cpu_idx);
    [[noreturn]] void start();
} // export namespace sched