// Copyright (C) 2024-2026  ilobilo

export module system.sched;

export import :cred;
export import :perm;
export import :nice;
export import :process;
export import :thread;
export import :run_queue;
export import :wait_queue;
export import :work_queue;

import :arch;

// implemented in :arch
namespace sched::arch
{
    struct context;
    struct data;

    // makes sure that thread doesn't migrate to another core
    thread_t *current_thread();

    void context_switch(thread_t *prev, thread_t *next);

    void init_thread(
        thread_t *thread, std::uintptr_t ip, std::uintptr_t arg,
        std::uintptr_t stack_top, bool is_kernel
    );

    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack);
} // namespace sched::arch

export namespace sched
{
    // called from bsp
    void init();

    // start scheduling on this code
    [[noreturn]] void start();

    inline thread_t *current_thread()
    {
        return arch::current_thread();
    }

    inline process_t *current_process()
    {
        return current_thread()->proc;
    }

    inline void preempt_disable()
    {
        current_thread()->preempt_count.fetch_add(1, std::memory_order_acquire);
    }

    inline void preempt_enable()
    {
        auto thread = current_thread();
        if (thread->preempt_count.fetch_sub(1, std::memory_order_release) == 1
            && thread->needs_resched())
            schedule();
    }

    inline bool is_preempt_disabled()
    {
        return current_thread()->preempt_count.load(std::memory_order_relaxed) > 0;
    }

    // pick next thread and switch to it
    // called on yield, block, timer or wake up
    void schedule();

    // create and run a new kernel thread under pid 0
    thread_t *create_kthread(nice_t nice, std::uintptr_t ip, std::uintptr_t arg);

    // create a user thread
    thread_t *create_thread(process_t *proc, int nice, std::uintptr_t entry, std::uintptr_t stack);

    bool wake_up(thread_t *thread, bool preempt = true);

    // put the current thread to sleep
    void sleep();

    // put the current thread to uninterruptible sleep
    void block();

    // sleep for nanoseconds. return remaining time if interrupted
    std::uint64_t sleep_for_ns(std::uint64_t ns);

    void yield();

    // exit the current thread
    // if this is the last thread, process becomes a zombie
    [[noreturn]] void thread_exit(int exit_code);

    // exit the current process and kill all threads
    [[noreturn]] void process_exit(int exit_code);

    // get process with the pid
    process_t *get_process(pid_t pid);

    void preempt_disable();
    void preempt_enable();
    bool is_preempt_disabled();

    // called from a timer interrupt
    void tick();

    void load_balance();

    void set_affinity(pid_t pid, lib::bitmap mask);
    lib::bitmap get_affinity(pid_t pid);

    int setpgid(pid_t pid, pid_t pgid);
    pid_t getpgid(pid_t pid);

    pid_t setsid();
    pid_t getsid(pid_t pid);

    struct kclone_args
    {
        std::uint64_t flags;
        int __user *pidfd;
        int __user *child_tid;
        int __user *parent_tid;
        int exit_signal;
        std::uint64_t stack;
        std::uint64_t stack_size;
        std::uint64_t tls;
        pid_t *set_tid;
        std::size_t set_tid_size;
        int cgroup;
    };

    pid_t clone(const kclone_args &args);
    int exec(const vfs::path &path, std::vector<std::string> argv, std::vector<std::string> envp);

    struct wait_options_t
    {
        enum {
            wnohang = 0x00000001,
            wuntraced = 0x00000002,
            wstopped = wuntraced,
            wexited = 0x00000004,
            wcontinued = 0x00000008,
            wnowait = 0x01000000
        };

        // -1 = any child
        // >0 = specific pid
        // 0 = same pgroupd
        // <-1 = specific pgroup
        pid_t pid;
        int options;
    };

    pid_t waitpid(const wait_options_t &options, int *status);
} // export namespace sched
