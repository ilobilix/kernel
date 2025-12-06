// Copyright (C) 2024-2025  ilobilo

module system.scheduler;

import drivers.timers;
import system.cpu.local;
import system.memory;
import system.chrono;
import system.acpi;
import magic_enum;
import frigg;
import boot;
import arch;
import lib;
import std;

namespace sched
{
    class percpu
    {
        private:
        template<typename MType, MType thread::*Member>
        class compare
        {
            public:
            bool operator()(const thread &lhs, const thread &rhs) const
            {
                return lhs.*Member < rhs.*Member;
            }
        };

        public:
        lib::locker<
            lib::rbtree<
                thread, &thread::rbtree_hook,
                compare<
                    std::uint64_t,
                    &thread::vruntime
                >
            >, lib::spinlock_preempt
        > queue;

        thread *running_thread;

        lib::rbtree<
            thread, &thread::rbtree_hook,
            compare<
                std::optional<std::size_t>,
                &thread::sleep_until
            >
        > sleep_queue;

        process *idle_proc;
        thread *idle_thread;
        thread *reaper_thread;

        lib::intrusive_list<thread, &thread::list_hook> dead_threads;

        std::atomic_size_t preemption = 0;
        std::atomic_bool in_scheduler = false;
    };

    cpu_local<percpu> percpu;
    cpu_local_init(percpu);

    namespace arch
    {
        void init();
        void reschedule(std::size_t ms);

        void finalise(process *proc, thread *thread, std::uintptr_t ip);
        void deinitialise(process *proc, thread *thread);

        void save(thread *thread);
        void load(thread *thread);

        void update_stack(thread *thread, std::uintptr_t addr);
    } // namespace arch

    namespace
    {
        lib::locker<
            lib::map::flat_hash<
                std::size_t, process *
            >, lib::rwspinlock
        > processes;

        lib::locker<
            lib::map::flat_hash<
                std::size_t, group *
            >, lib::rwspinlock
        > groups;

        lib::locker<
            lib::map::flat_hash<
                std::size_t, session *
            >, lib::rwspinlock
        > sessions;

        bool initialised = false;

        std::size_t alloc_pid(process *proc)
        {
            static std::atomic_size_t next_pid = 0;
            const auto pid = next_pid++;
            processes.write_lock().value()[pid] = proc;
            return pid;
        }

        void save(thread *thread, cpu::registers *regs)
        {
            std::memcpy(&thread->regs, regs, sizeof(cpu::registers));
            arch::save(thread);
        }

        void load(bool same_pid, thread *thread, cpu::registers *regs)
        {
            std::memcpy(regs, &thread->regs, sizeof(cpu::registers));
            arch::load(thread);
            if (!same_pid)
                thread->parent->vmspace->pmap->load();
        }

        void idle()
        {
            ::arch::halt(true);
        }
    } // namespace

    bool is_initialised() { return initialised; }

    group *create_group(process *proc)
    {
        auto grp = new group { };
        grp->pgid = proc->pgid = proc->pid;
        grp->members.write_lock().value()[proc->pid] = proc;
        groups.write_lock().value()[grp->pgid] = grp;
        return grp;
    }

    session *create_session(group *grp)
    {
        auto sess = new session { };
        sess->sid = grp->pgid;
        sess->members.write_lock().value()[grp->pgid] = grp;
        sessions.write_lock().value()[sess->sid] = sess;
        return sess;
    }

    bool change_group(process *proc, group *grp)
    {
        const auto wlocked = grp->members.write_lock();
        const auto [it, inserted] = wlocked->emplace(proc->pid, proc);
        if (inserted)
        {
            if (auto old_group = group_for(proc->pgid))
            {
                const auto owlocked = old_group->members.write_lock();
                const auto end = owlocked->end();
                auto oit = owlocked->find(proc->pid);
                lib::bug_on(oit == end);
                owlocked->erase(oit);
            }
            proc->pgid = grp->pgid;
            proc->sid = grp->sid;
            return true;
        }
        return false;
    }

    bool change_session(group *grp, session *sess)
    {
        const auto wlocked = sess->members.write_lock();
        const auto [it, inserted] = wlocked->emplace(grp->pgid, grp);
        if (inserted)
        {
            if (auto old_session = session_for(grp->sid))
            {
                const auto owlocked = old_session->members.write_lock();
                const auto end = owlocked->end();
                auto oit = owlocked->find(grp->pgid);
                lib::bug_on(oit == end);
                owlocked->erase(oit);
            }
            grp->sid = sess->sid;
            for (auto &[pid, proc] : grp->members.write_lock().value())
                proc->sid = sess->sid;
            return true;
        }
        return false;
    }

    process *proc_for(pid_t pid)
    {
        if (pid == -1)
            return percpu->idle_proc;

        process *ret = nullptr;
        {
            const auto rlocked = processes.read_lock();
            const auto it = rlocked->find(pid);
            if (it == rlocked->end())
                return nullptr;
            ret = it->second;
        }
        return ret;
    }

    group *group_for(pid_t pgid)
    {
        const auto rlocked = groups.read_lock();
        const auto it = rlocked->find(pgid);
        if (it == rlocked->end())
            return nullptr;
        return it->second;
    }

    session *session_for(pid_t sid)
    {
        const auto rlocked = sessions.read_lock();
        const auto it = rlocked->find(sid);
        if (it == rlocked->end())
            return nullptr;
        return it->second;
    }

    void thread::update_ustack(std::uintptr_t addr)
    {
        lib::bug_on(!is_user);
        arch::update_stack(this, addr);
    }

    void thread::prepare_sleep(std::size_t ms)
    {
        sleep_ints = ::arch::int_switch_status(false);
        sleep_lock.lock();
        status = status::sleeping;

        if (ms)
            sleep_for = ms * 1'000'000;
        else
            sleep_for = std::nullopt;
    }

    bool thread::wake_up(std::size_t reason)
    {
        const bool ints = ::arch::int_switch_status(false);
        sleep_lock.lock();

        if (status != status::sleeping)
        {
            sleep_lock.unlock();
            ::arch::int_switch(ints);
            return false;
        }

        status = status::ready;
        wake_reason = reason;

        sleep_lock.unlock();
        ::arch::int_switch(ints);
        return true;
    }

    thread::~thread()
    {
        lib::free(kstack_top - boot::kstack_size);

        if (is_user)
        {
            const auto obj_ref = ustack_obj.use_count();
            const auto &vmspace = parent->vmspace;
            // const auto vaddr = ustack_top - boot::ustack_size;
            // lib::panic_if(!vmspace->unmap(vaddr, boot::ustack_size));
            lib::panic_if(!vmspace->unmap(ustack_obj.lock()));
            lib::panic_if(ustack_obj.use_count() != obj_ref - 1);
        }

        arch::deinitialise(parent, this);

        // lib::error("TODO: thread {} deconstructor on cpu {}", tid, cpu::self()->idx);
    }

    thread *thread::create(process *parent, std::uintptr_t ip, bool is_user)
    {
        lib::bug_on(!parent);
        auto thread = new sched::thread { };

        thread->tid = parent->next_tid++;
        thread->parent = parent;
        thread->status = status::not_ready;
        thread->is_user = is_user;
        thread->priority = default_prio;
        thread->vruntime = 0;

        const auto stack = lib::alloc<std::uintptr_t>(boot::kstack_size) + boot::kstack_size;
        thread->kstack_top = stack;

        if (is_user)
        {
            auto &vmspace = parent->vmspace;

            const auto vaddr = (parent->next_stack_top -= boot::ustack_size);
            auto obj = std::make_shared<vmm::memobject>();

            lib::panic_if(!vmspace->map(
                vaddr, boot::ustack_size,
                vmm::prot::read | vmm::prot::write,
                vmm::flag::private_ | vmm::flag::untouchable, obj, 0
            ));

            thread->ustack_obj = obj;
            thread->ustack_top = vaddr + boot::ustack_size;
        }
        else thread->ustack_top = stack;

        arch::finalise(parent, thread, ip);

        const std::unique_lock _ { parent->lock };
        parent->threads[thread->tid] = thread;

        return thread;
    }

    process::~process()
    {
        lib::panic("TODO: process {} deconstructor", pid);
    }

    process *process::create(process *parent, std::shared_ptr<vmm::pagemap> pagemap)
    {
        lib::bug_on(!pagemap);
        auto proc = new process { };

        proc->pid = alloc_pid(proc);
        if (proc->pid != 0)
        {
            if (proc->pid == 1)
            {
                lib::bug_on(parent != nullptr);
                // proc->pgid is set in create_group
                auto grp = create_group(proc);
                grp->sid = proc->sid = create_session(grp)->sid;
            }
            else if (parent)
            {
                proc->pgid = parent->pgid;
                proc->sid = parent->sid;
                {
                    auto grp = group_for(proc->pgid);
                    auto wlocked = grp->members.write_lock();
                    lib::bug_on(wlocked->contains(proc->pid));
                    wlocked.value()[proc->pid] = proc;
                }
            }
            else lib::panic("cannot create a non-init process without a parent");
        }

        proc->vmspace = std::make_shared<vmm::vmspace>(pagemap);

        if (parent)
        {
            proc->root = parent->root;
            proc->cwd = parent->cwd;
            proc->parent = parent;

            const std::unique_lock _ { parent->lock };
            parent->children[proc->pid] = proc;
        }
        else proc->root = proc->cwd = vfs::get_root(true);

        return proc;
    }

    bool process::fdtable::close(int fd)
    {
        return fds.write_lock()->erase(fd);
    }

    std::shared_ptr<vfs::filedesc> process::fdtable::get(int fd)
    {
        const auto rlocked = fds.read_lock();
        auto it = rlocked->find(fd);
        if (it == rlocked->end())
            return nullptr;
        return it->second;
    }

    int process::fdtable::allocate_fd(std::shared_ptr<vfs::filedesc> desc, int fd, bool force)
    {
        auto wlocked = fds.write_lock();
        if (wlocked->contains(fd))
        {
            if (!force)
            {
                fd = next_fd++;
                while (wlocked->contains(fd))
                    fd++;
            }
            else lib::bug_on(!wlocked->erase(fd));
        }

        wlocked.value()[fd] = desc;
        return fd;
    }

    int process::fdtable::dup(int oldfd, int newfd, bool closexec, bool force)
    {
        if (oldfd < 0 || newfd < 0)
            return (errno = EBADF, -1);
        auto fdesc = get(oldfd);
        if (!fdesc)
            return (errno = EBADF, -1);

        const auto newfdesc = std::make_shared<vfs::filedesc>(fdesc->file, closexec);
        const auto fd = allocate_fd(newfdesc, newfd, force);
        if (fd < 0)
            return (errno = EMFILE, -1);
        fdesc->file->ref.fetch_add(1);
        return fd;
    }

    thread *this_thread()
    {
        return percpu->running_thread;
    }

    std::size_t sleep_for(std::size_t ms)
    {
        this_thread()->prepare_sleep(ms);
        return yield();
    }

    std::size_t yield()
    {
        auto thread = this_thread();

        const bool eeping = thread->status == status::sleeping;
        const bool old = eeping ? thread->sleep_ints : ::arch::int_switch_status(false);

        if (eeping)
        {
            if (thread->sleep_for.has_value())
            {
                const auto clock = chrono::main_clock();
                thread->sleep_until = clock->ns() + thread->sleep_for.value();
                thread->sleep_for = std::nullopt;
            }
            percpu->sleep_queue.insert(thread);
        }

        ::arch::int_switch(true);
        arch::reschedule(0);
        ::arch::int_switch(old);

        return eeping ? thread->wake_reason : wake_reason::success;
    }

    std::size_t allocate_cpu()
    {
        std::size_t idx = 0;
        std::size_t min = std::numeric_limits<std::size_t>::max();

        for (std::size_t i = 0; i < cpu::count(); i++)
        {
            auto &obj = percpu.get(cpu::local::nth_base(i));
            const auto size = obj.queue.lock()->size();
            if (size < min)
            {
                min = size;
                idx = i;
            }
        }
        return idx;
    }

    void enqueue(thread *thread, std::size_t cpu_idx)
    {
        auto &obj = percpu.get(cpu::local::nth_base(cpu_idx));
        switch (thread->status)
        {
            [[unlikely]] case status::killed:
            [[unlikely]] case status::not_ready:
            [[unlikely]] case status::sleeping:
                lib::panic(
                    "can't enqueue a thread that is {}",
                    magic_enum::enum_name(thread->status)
                );
                break;
            [[likely]] case status::running:
                thread->status = status::ready;
                [[fallthrough]];
            case status::ready:
                obj.queue.lock()->insert(thread);
                break;
            default:
                std::unreachable();
        }
    }

    thread *spawn(pid_t pid, std::uintptr_t ip, nice_t priority)
    {
        return spawn_on(allocate_cpu(), pid, ip, priority);
    }

    thread *spawn_on(std::size_t cpu, pid_t pid, std::uintptr_t ip, nice_t priority)
    {
        const auto thread = thread::create(processes.read_lock()->at(pid), ip, false);
        thread->priority = priority;
        thread->status = status::ready;
        enqueue(thread, cpu);
        return thread;
    }

    void reaper()
    {
        auto &dead = percpu->dead_threads;
        auto &me = percpu->reaper_thread;
        while (true)
        {
            while (true)
            {
                if (dead.empty())
                {
                    me->status = status::sleeping;
                    lib::bug_on(me->sleep_until.has_value());
                    yield();
                    lib::bug_on(dead.empty());
                }
                break;
            }

            // for batch reaping
            me->prepare_sleep(timeslice * 5);
            yield();

            disable();
            auto list = std::move(dead);
            enable();

            while (!list.empty())
            {
                // TODO
                const auto thread = list.front();
                list.pop_front();

                auto proc = thread->parent;
                lib::bug_on(proc->threads.erase(thread->tid) != 1);

                delete thread;
                if (proc->threads.empty())
                    lib::panic("TODO: process {} exit", proc->pid);

                // TODO: thread, process, group and session cleanup
            }
        }
        std::unreachable();
    }

    void sleeper()
    {
        auto &eepers = percpu->sleep_queue;
        auto &dead = percpu->dead_threads;
        while (true)
        {
            while (true)
            {
                if (!eepers.empty())
                    break;
                yield();
            }

            const auto clock = chrono::main_clock();
            const auto time = clock->ns();

            disable();
            const auto begin = eepers.begin();
            for (auto it = begin; it != eepers.end(); )
            {
                const auto thread = (it++).value();
                if (!thread->sleep_until.has_value())
                {
                    switch (thread->status)
                    {
                        case status::ready:
                            eepers.remove(thread);
                            percpu->queue.lock()->insert(thread);
                            break;
                        case status::killed:
                            eepers.remove(thread);
                            dead.push_back(thread);
                            break;
                        case status::not_ready:
                        case status::sleeping:
                            continue;
                        case status::running:
                            lib::panic("found a running thread in sleep queue");
                            std::unreachable();
                        default:
                            std::unreachable();
                    }
                }
                else
                {
                    if (time < thread->sleep_until.value())
                        break;

                    eepers.remove(thread);

                    thread->sleep_until = std::nullopt;
                    thread->status = status::ready;
                    thread->wake_reason = wake_reason::success;
                    {
                        // do not starve out other threads
                        // if the woken one has been sleeping for too long
                        auto qlocked = percpu->queue.lock();
                        thread->vruntime = begin->vruntime;
                        qlocked->insert(thread);
                    }
                }
            }
            enable();
            yield();
        }
        std::unreachable();
    }

    void schedule(cpu::registers *regs)
    {
        auto &pcpu = percpu.get();
        if (pcpu.preemption.load(std::memory_order_acquire) > 0)
        {
            arch::reschedule(timeslice);
            return;
        }
        pcpu.in_scheduler.store(true, std::memory_order_release);

        const auto clock = chrono::main_clock();
        const auto time = clock->ns();

        const auto self = cpu::self();
        auto &dead = pcpu.dead_threads;

        thread *next = nullptr;
        bool found_dead = false;

        {
            auto locked = pcpu.queue.lock();

            const auto begin = locked->begin();
            for (auto it = begin; it != locked->end(); )
            {
                const auto thread = (it++).value();
                switch (thread->status)
                {
                    case status::ready:
                        next = thread;
                        locked->remove(thread);
                        goto found;
                    [[unlikely]] case status::sleeping:
                    [[unlikely]] case status::running:
                        lib::panic(
                            "found a {} thread in scheduler queue",
                            magic_enum::enum_name(thread->status)
                        );
                        std::unreachable();
                    case status::killed:
                        locked->remove(thread);
                        dead.push_back(thread);
                        found_dead = true;
                        break;
                    [[unlikely]] case status::not_ready:
                        break;
                    default:
                        std::unreachable();
                }
            }
            found:
        }

        const auto current = pcpu.running_thread;
        const bool is_current_idle = (current == pcpu.idle_thread);

        std::optional<pid_t> prev_pid { };
        if (current) [[likely]]
        {
            if (current->status != status::killed)
            {
                prev_pid = current->parent->pid;
                if (!is_current_idle) [[likely]]
                {
                    if (next == nullptr && current->status == status::running) [[unlikely]]
                        next = current;
                    else if (current->status == status::sleeping)
                        current->sleep_lock.unlock();

                    static constexpr std::size_t weight0 = prio_to_weight(0);
                    const std::size_t exec_time = time - current->schedule_time;
                    const std::size_t weight = prio_to_weight(current->priority);
                    const std::size_t vtime = (exec_time * weight0) / weight;
                    current->vruntime += vtime;

                    if (next != current) [[likely]]
                    {
                        save(current, regs);
                        if (current->status != status::sleeping)
                            enqueue(current, self->idx);
                    }
                }
                // should it save idle thread ctx?
                else save(current, regs);
            }
            else
            {
                dead.push_back(current);
                found_dead = true;
            }
        }

        if (found_dead)
        {
            auto &reaper = percpu->reaper_thread;
            // if the reaper is sleeping and not waiting for a timeout
            if (reaper->status == status::sleeping && reaper->sleep_until == std::nullopt)
                reaper->status = status::ready;
        }

        if (next == nullptr) [[unlikely]]
            next = pcpu.idle_thread;

        if (next != current) [[likely]]
        {
            next->running_on = self->self;
            next->status = status::running;
            pcpu.running_thread = next;

            const bool same_pid = (prev_pid.has_value() && prev_pid.value() == next->parent->pid);
            load(same_pid, next, regs);
        }

        if (!is_current_idle) [[likely]]
            next->schedule_time = clock->ns();

        arch::reschedule(timeslice);
        pcpu.in_scheduler.store(false, std::memory_order_release);
    }

    lib::initgraph::stage *pid0_initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "sched.pid0.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task pid0_task
    {
        "sched.pid0.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { ::arch::cpus_stage(), timers::initialised_stage() },
        lib::initgraph::entail { pid0_initialised_stage() },
        [] {
            auto proc = process::create(
                nullptr,
                std::make_shared<vmm::pagemap>(vmm::kernel_pagemap.get())
            );
            lib::bug_on(proc->pid != 0);
        }
    };

    void enable()
    {
        if (!initialised)
            return;

        auto &pcpu = percpu.get();
        if (pcpu.in_scheduler.load(std::memory_order_acquire))
            return;

        std::size_t expected = pcpu.preemption.load(std::memory_order_relaxed);
        while (expected > 0 &&
            !pcpu.preemption.compare_exchange_weak(
                expected, expected - 1,
                std::memory_order_acq_rel, std::memory_order_relaxed
            )
        ) {
            ::arch::pause();
        }
    }

    void disable()
    {
        if (!initialised)
            return;

        auto &pcpu = percpu.get();
        if (pcpu.in_scheduler.load(std::memory_order_acquire))
            return;

        pcpu.preemption.fetch_add(1, std::memory_order_release);
    }

    [[noreturn]] void start()
    {
        static std::atomic_bool should_start = false;

        static auto idle_vmspace = std::make_shared<vmm::vmspace>(
            std::make_shared<vmm::pagemap>(vmm::kernel_pagemap.get())
        );

        const auto self = cpu::self();

        const auto idle_proc = new process { };
        idle_proc->vmspace = idle_vmspace;
        idle_proc->pid = -1;

        const auto idle_thread = thread::create(idle_proc, reinterpret_cast<std::uintptr_t>(idle), false);
        idle_thread->status = status::ready;

        percpu->idle_proc = idle_proc;
        percpu->idle_thread = idle_thread;

        arch::init();
        ::arch::int_switch(true);

        if (self->idx == cpu::bsp_idx())
        {
            for (std::size_t idx = 0; idx < cpu::count(); idx++)
            {
                auto &obj = percpu.get(cpu::local::nth_base(idx));
                obj.reaper_thread = sched::spawn_on(idx, 0, reinterpret_cast<std::uintptr_t>(reaper), nice_t::max);
                sched::spawn_on(idx, 0, reinterpret_cast<std::uintptr_t>(sleeper), -5);
            }

            initialised = true;
            should_start = true;
        }

        while (!should_start)
            ::arch::pause();

        arch::reschedule(0);
        ::arch::halt(true);
    }
} // namespace sched