// Copyright (C) 2024-2026  ilobilo

export module system.sched:process;

import system.sched.wait_queue;
import system.sched.mutex;
import system.sched.cred;
import system.memory.virt;
import system.vfs;
import lib;
import std;

import :signal;
import :rlimit;
import :sleep;
import :thread;

export namespace sched
{
    struct group_t;
    struct session_t;

    struct ctty_base
    {
        virtual ~ctty_base() = default;
        virtual void detach(session_t *session) = 0;
    };

    enum class dumpable_t : std::uint8_t
    {
        disable = 0,
        user = 1,
        root = 2
    };

    struct process_t
    {
        pid_t pid;

        process_t *parent;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                process_t *
            >, mutex
        > children;

        std::shared_ptr<group_t> group;
        std::shared_ptr<session_t> session;

        std::shared_ptr<vmm::vmspace> vmspace;

        struct vfs_state
        {
            vfs::path root;
            vfs::path cwd;
            mode_t umask = static_cast<mode_t>(s_iwgrp | s_iwoth);

            std::shared_ptr<vfs_state> clone() const
            {
                auto cloned = std::make_shared<vfs_state>(*this);
                cloned->root = root;
                cloned->cwd = cwd;
                cloned->umask = umask;
                return cloned;
            }
        };
        std::shared_ptr<vfs_state> vfs;
        std::shared_ptr<vfs::fdtable> fdt;

        std::shared_ptr<cred_t> cred;
        std::shared_ptr<rlimits_t> rlimits;

        std::shared_ptr<signal_actions_t> sigactions;
        signal_queue_t sigqueue;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                thread_t *
            >, mutex
        > threads;

        std::atomic<std::size_t> alive_threads = 0;
        std::atomic<std::size_t> to_quiesce = 0;

        std::atomic<dumpable_t> dumpable = dumpable_t::user;

        int exit_code = 0;
        int term_signal = 0;
        // this saves 8 bytes :trl:
        bool killed_by_signal : 1 = false;
        bool dumped_core : 1 = false;
        bool is_zombie : 1 = false;
        bool has_execved : 1 = false;
        bool vfork_pending : 1 = false;

        bool pending_continued : 1 = false;
        int pending_stop_sig = 0;
        lib::spinlock report_lock;

        wait_queue_t wait_child;
        wait_queue_t vfork_done;

        alarm_entry_t alarm { };
        cpu_itimer_t itimer_virtual { };
        cpu_itimer_t itimer_prof { };

        recursive_mutex lock;
    };

    struct group_t
    {
        pid_t pgid;
        std::shared_ptr<session_t> session;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                process_t *
            >, mutex
        > members;

        int signal_all(int sig);
    };

    struct session_t
    {
        pid_t sid;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                std::weak_ptr<group_t>
            >, mutex
        > members;

        lib::locker<
            std::shared_ptr<
                ctty_base
            >, mutex
        > ctty;
    };
} // export namespace sched
