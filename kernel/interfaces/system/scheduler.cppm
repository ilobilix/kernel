// Copyright (C) 2024-2025  ilobilo

export module system.scheduler;

import system.scheduler.base;
import system.memory.virt;
import system.cpu.self;
import system.cpu;
import system.vfs;
import frigg;
import lib;
import cppstd;

namespace cpu
{
    extern "C++" struct processor;
} // namespace cpu

namespace sched
{
    constexpr std::size_t timeslice = 6;

    using nice_t = lib::ranged<std::int8_t, -20, 19>;
    constexpr nice_t default_prio = 0;

    inline constexpr std::size_t prio_to_weight(nice_t prio)
    {
        static constexpr std::size_t table[40]
        {
            88761, 71755, 56483, 46273, 36291,
            29154, 23254, 18705, 14949, 11916,
            9548,  7620,  6100,  4904,  3906,
            3121,  2501,  1991,  1586,  1277,
            1024,  820,   655,   526,   423,
            335,   272,   215,   172,   137,
            110,   87,    70,    56,    45,
            36,    29,    23,    18,    15,
        };
        return table[prio + 20];
    }
} // namespace sched

export namespace sched
{
    enum class status
    {
        not_ready,
        ready,
        running,
        sleeping,
        killed
    };

    enum wake_reason
    {
        success = 0,
        interrupted = 1
    };

    struct process;
    struct thread : thread_base
    {
        // do not move
        cpu::processor *running_on;
        std::uintptr_t ustack_top;
        std::uintptr_t kstack_top;

        std::weak_ptr<vmm::memobject> ustack_obj;

        pid_t tid;
        process *parent;

        status status;
        bool is_user;

        cpu::registers regs;

        nice_t priority;

        std::uintptr_t set_child_tid = 0;
        std::uintptr_t clear_child_tid = 0;

        std::uint64_t vruntime = 0;
        std::uint64_t schedule_time;

        lib::spinlock sleep_lock;
        bool sleep_ints;
        std::size_t wake_reason;
        std::optional<std::size_t> sleep_for;
        std::optional<std::size_t> sleep_until;

#if defined(__x86_64__)
        std::uintptr_t gs_base;
        std::uintptr_t fs_base;

        std::byte *fpu;
        std::size_t fpu_size;
#elif defined(__aarch64__)
#endif

        errnos err = no_error;

        lib::rbtree_hook rbtree_hook;
        frg::default_list_hook<thread> list_hook;

        void update_ustack(std::uintptr_t addr);

        void prepare_sleep(std::size_t ms = 0);
        bool wake_up(std::size_t reason);

        static thread *create(process *parent, std::uintptr_t ip, bool is_user);

        thread() = default;
        ~thread();
    };

    struct process
    {
        static constexpr std::uintptr_t initial_stck_top = 0x7FF'FFF'FFF'000;

        pid_t pid;
        pid_t pgid;
        pid_t sid;

        gid_t rgid = 0, sgid = 0, egid = 0;
        uid_t ruid = 0, suid = 0, euid = 0;

        std::shared_ptr<vmm::vmspace> vmspace;

        vfs::path root;
        vfs::path cwd;
        mode_t umask = static_cast<mode_t>(s_iwgrp | s_iwoth);
        vfs::fdtable fdt;

        bool has_execved = false;

        lib::spinlock lock;

        process *parent;
        lib::map::flat_hash<pid_t, process *> children;
        lib::map::flat_hash<pid_t, thread *> threads;

        std::atomic<pid_t> next_tid = 1;
        std::uintptr_t next_stack_top = initial_stck_top;

        static process *create(process *parent, std::shared_ptr<vmm::pagemap> pagemap);

        process() = default;
        ~process();
    };

    struct group
    {
        pid_t pgid;
        pid_t sid;
        lib::locker<
            lib::map::flat_hash<
                pid_t, process *
            >, lib::rwspinlock
        > members;
    };

    struct session
    {
        pid_t sid;
        lib::locker<
            lib::map::flat_hash<
                pid_t, std::shared_ptr<group>
            >, lib::rwspinlock
        > members;
    };

    bool is_initialised();

    process *proc_for(pid_t pid);

    std::shared_ptr<group> group_for(pid_t pgid);
    std::shared_ptr<session> session_for(pid_t sid);

    std::shared_ptr<group> create_group(process *proc);
    std::shared_ptr<session> create_session(std::shared_ptr<group> grp);

    bool change_group(process *proc, std::shared_ptr<group> grp);
    bool change_session(std::shared_ptr<group> grp, std::shared_ptr<session> sess);

    thread *this_thread();

    std::size_t sleep_for(std::size_t ms);
    std::size_t yield();

    std::size_t allocate_cpu();
    void enqueue(thread *thread, std::size_t cpu_idx);

    thread *spawn(pid_t pid, std::uintptr_t ip, nice_t priority = default_prio);
    thread *spawn_on(std::size_t cpu, pid_t pid, std::uintptr_t ip, nice_t priority = default_prio);

    void enable();
    void disable();

    lib::initgraph::stage *pid0_initialised_stage();

    [[noreturn]] void start();
} // export namespace sched