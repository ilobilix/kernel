// Copyright (C) 2024-2026  ilobilo

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
            static bool operator()(const thread &lhs, const thread &rhs)
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

        lib::locker<
            lib::rbtree<
                thread, &thread::rbtree_hook,
                compare<
                    std::optional<std::size_t>,
                    &thread::sleep_until
                >
            >, lib::spinlock_preempt
        > sleep_queue;

        process *idle_proc;
        thread *idle_thread;
        thread *reaper_thread;
        thread *sleeper_thread;

        lib::intrusive_list<thread, &thread::list_hook> dead_threads;

        std::atomic_bool in_scheduler = false;
        std::atomic_bool idling = false;
    };

    cpu_local(percpu, percpu);

    namespace arch
    {
        using namespace ::arch;

        void init();

        void enable();
        void disable();

        void reschedule(std::size_t ms);
        void reschedule_other(std::size_t cpu);

        void initialise(process *proc, thread *thrd, std::uintptr_t ip, std::uintptr_t arg, bool cloning);
        void deinitialise(process *proc, thread *thread);

        [[noreturn]]
        void enter_user(thread *thread, std::uintptr_t ip, std::uintptr_t stack);

        void save(thread *thread);
        void load(thread *thread);

        void ctx_switch(thread *current, thread *next);
    } // namespace arch

    namespace
    {
        std::atomic<pid_t> next_pid = 2;
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

        std::atomic_bool initialised = false;
        process *pid0 = nullptr;

        std::shared_ptr<vmm::vmspace> kvmspace = nullptr;
        lib::spinlock kvmspace_lock;

        void save(thread *thread, cpu::registers *regs)
        {
            if (regs)
                std::memcpy(&thread->regs, regs, sizeof(cpu::registers));
            arch::save(thread);
        }

        void load(bool same_pid, thread *thread, cpu::registers *regs)
        {
            if (regs)
                std::memcpy(regs, &thread->regs, sizeof(cpu::registers));
            arch::load(thread);
            if (!same_pid)
                thread->parent->vmspace->pmap->load();
        }

        void idle()
        {
            arch::halt(true);
        }

        thread *spawn_on(std::size_t cpu, std::uintptr_t ip, std::uintptr_t arg, nice_t priority)
        {
            const auto thread = thread::create(pid0, ip, arg, false);
            thread->priority = priority;
            thread->status = status::ready;
            enqueue(thread, cpu);
            return thread;
        }
    } // namespace

    // TODO: better pid/tid allocation
    pid_t alloc_pid(process *proc)
    {
        const auto pid = next_pid++;
        processes.write_lock().value()[pid] = proc;
        return pid;
    }

    pid_t alloc_tid()
    {
        return next_pid++;
    }

    thread *spawn(std::uintptr_t ip, std::uintptr_t arg, nice_t priority)
    {
        return spawn_on(allocate_cpu(), ip, arg, priority);
    }

    bool is_initialised() { return initialised; }
    process *get_pid0() { return pid0; }

    group *create_group(pid_t pgid)
    {
        return groups.write_lock().value()[pgid] = new group {
            .pgid = pgid,
            .sid = 0,
            .members = { }
        };
    }

    session *create_session(pid_t sid)
    {
        return sessions.write_lock().value()[sid] = new session {
            .sid = sid,
            .members = { },
            .controlling_tty = nullptr
        };
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
            return percpu.read<process *, &percpu::idle_proc>();

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

    void thread::kill()
    {
        const auto old_status = status;
        status = status::killed;
        if (old_status == status::running)
            arch::reschedule_other(running_on->idx);
    }

    [[noreturn]]
    void thread::enter_user(std::uintptr_t ip, std::uintptr_t stack)
    {
        lib::bug_on(!is_user);
        arch::enter_user(this, ip, stack);
    }

    void thread::prepare_sleep(std::size_t ms)
    {
        sleep_ints = arch::int_switch_status(false);
        sleep_lock.lock();
        status = status::sleeping;

        if (ms)
            sleep_for = ms * 1'000'000;
        else
            sleep_for = std::nullopt;

        sleep_until = std::nullopt;
    }

    void thread::cancel_sleep()
    {
        if (status != status::sleeping)
            return;

        status = status::running;
        sleep_for = std::nullopt;
        sleep_lock.unlock();

        arch::int_switch(sleep_ints);
    }

    bool thread::wake_up(std::size_t reason)
    {
        const bool ints = arch::int_switch_status(false);
        sleep_lock.lock();

        if (status != status::sleeping)
        {
            sleep_lock.unlock();
            arch::int_switch(ints);
            return false;
        }

        wake_reason = reason;

        const auto target_cpu = running_on->idx;
        bool was_idling = false;
        {
            auto &pcpu = percpu.unsafe_get(cpu::local::nth_base(target_cpu));
            auto our_qlocked = pcpu.queue.lock();
            // if sleep_until isn't set, then this thread was sleeping without a timeout
            // that means that it will not be woken up by the sleeper thread
            // it's our job to insert it back in the run queue
            if (!sleep_until.has_value())
            {
                status = status::ready;
                if (!our_qlocked->empty())
                    vruntime = our_qlocked->first()->vruntime;
                our_qlocked->insert(this);
            }
            // force timeout to expire
            else sleep_until = 0;

            auto &our_eeper = pcpu.sleeper_thread;
            if (our_eeper->status == status::sleeping)
            {
                our_eeper->sleep_lock.unlock();
                our_eeper->status = status::ready;
                our_eeper->wake_reason = wake_reason::success;
                if (!our_qlocked->empty())
                    our_eeper->vruntime = our_qlocked->first()->vruntime;
                our_qlocked->insert(our_eeper);
            }

            if (pcpu.idling.load(std::memory_order_acquire))
            {
                pcpu.idling.store(false, std::memory_order_release);
                was_idling = true;
            }
        }

        sleep_lock.unlock();

        // TODO: maybe not interrupt other cpus if they're doing stuff
        disable();
        const auto me = cpu::self().unsafe_get().idx;
        if (target_cpu != me || was_idling)
            arch::reschedule_other(target_cpu);
        enable();

        arch::int_switch(ints);
        return true;
    }

    thread::~thread()
    {
        lib::free(kstack_top - boot::kstack_size);

        if (is_user && og_ustack_top)
        {
            const auto &vmspace = parent->vmspace;
            const auto vaddr = og_ustack_top - boot::ustack_size;
            lib::bug_on(!vmspace->unmap(vaddr, boot::ustack_size));
        }

        arch::deinitialise(parent, this);
    }

    thread *thread::create(
        process *parent, std::uintptr_t ip, std::uintptr_t arg,
        bool is_user, bool cloning
    )
    {
        lib::bug_on(!parent);
        auto thread = new sched::thread { };

        bool empty = false;
        {
            const std::unique_lock _ { parent->lock };
            empty = parent->threads.empty();
        }

        thread->tid = empty ? parent->pid : alloc_tid();
        thread->parent = parent;
        thread->status = status::not_ready;
        thread->is_user = is_user;
        thread->priority = default_prio;
        thread->vruntime = 0;
        thread->running_on = nullptr;

        const auto kstack = lib::alloc<std::uintptr_t>(boot::kstack_size) + boot::kstack_size;
        thread->kstack_top = kstack;

        if (is_user)
        {
            if (!cloning)
            {
                thread->in_trampoline = true;
                thread->og_ustack_top = thread->ustack_top = parent->map_stack();
            }
            else thread->in_trampoline = false;
        }
        else thread->og_ustack_top = thread->ustack_top = kstack;

        arch::initialise(parent, thread, ip, arg, cloning);

        const std::unique_lock _ { parent->lock };
        parent->threads[thread->tid] = thread;

        return thread;
    }

    std::uintptr_t process::map_stack()
    {
        const auto vaddr = (next_stack_top -= boot::ustack_size);
        const auto prot = vmm::read | vmm::write;
        const auto flags = vmm::private_ | vmm::anonymous | vmm::fixed_noreplace;

        lib::panic_if(!vmspace->map(
            vaddr, boot::ustack_size,
            prot, prot, flags, nullptr, 0
        ));

        // TODO: guard page

        return vaddr + boot::ustack_size;
    }

    process::~process()
    {
        lib::bug_on(!threads.empty());
        lib::bug_on(!children.empty());

        if (auto grp = group_for(pgid))
        {
            auto wlocked = grp->members.write_lock();
            wlocked->erase(pid);

            if (wlocked->empty())
            {
                wlocked.unlock();

                if (auto sess = session_for(grp->sid))
                {
                    auto swlocked = sess->members.write_lock();
                    swlocked->erase(grp->pgid);

                    if (swlocked->empty())
                    {
                        swlocked.unlock();
                        sessions.write_lock()->erase(sess->sid);
                        delete sess;
                    }
                }

                groups.write_lock()->erase(grp->pgid);
                delete grp;
            }
        }

        processes.write_lock()->erase(pid);
    }

    bool process::fdtable::close(int fd)
    {
        auto fdesc = get(fd);
        if (!fdesc)
            return false;

        if (!fds.write_lock()->erase(fd))
            return false;

        if (fdesc->file && fdesc->file->ref.fetch_sub(1) == 1)
        {
            if (const auto ret = fdesc->file->close(); !ret)
                lib::error("failed to close fd: {}", magic_enum::enum_name(ret.error()));
        }

        return true;
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
                next_fd = fd + 1;
            }
            else lib::bug_on(!wlocked->erase(fd));
        }
        else if (fd >= next_fd)
            next_fd = fd + 1;

        wlocked.value()[fd] = std::move(desc);
        return fd;
    }

    int process::fdtable::dup(int oldfd, int newfd, bool closexec, bool force)
    {
        if (oldfd < 0 || newfd < 0)
            return (errno = EBADF, -1);
        auto fdesc = get(oldfd);
        if (!fdesc)
            return (errno = EBADF, -1);

        auto newfdesc = std::make_shared<vfs::filedesc>(fdesc->file, closexec);
        const auto fd = allocate_fd(std::move(newfdesc), newfd, force);
        if (fd < 0)
            return (errno = EMFILE, -1);
        fdesc->file->ref.fetch_add(1);
        return fd;
    }

    void process::fdtable::close_on_exec()
    {
        auto wlocked = fds.write_lock();
        for (auto it = wlocked->begin(); it != wlocked->end(); )
        {
            if (it->second && it->second->closexec.load(std::memory_order_relaxed))
            {
                auto &fdesc = it->second;
                if (fdesc->file && fdesc->file->ref.fetch_sub(1) == 1)
                {
                    if (const auto ret = fdesc->file->close(); !ret)
                        lib::error("failed to close fd: {}", magic_enum::enum_name(ret.error()));
                }
                it = wlocked->erase(it);
            }
            else it++;
        }
    }

    process::fdtable::fdtable(fdtable &other) : next_fd { other.next_fd }
    {
        auto orlocked = other.fds.read_lock();
        auto wlocked = fds.write_lock();

        for (const auto &[fd, old_desc] : *orlocked)
        {
            if (!old_desc)
                continue;

            old_desc->file->ref.fetch_add(1, std::memory_order_relaxed);
            wlocked.value()[fd] = std::make_shared<vfs::filedesc>(
                old_desc->file,
                old_desc->closexec.load(std::memory_order_relaxed)
            );
        }
    }

    process::fdtable::~fdtable()
    {
        auto wlocked = fds.write_lock();
        for (auto &[fd, fdesc] : *wlocked)
        {
            if (fdesc && fdesc->file && fdesc->file->ref.fetch_sub(1) == 1)
            {
                if (const auto ret = fdesc->file->close(); !ret)
                    lib::error("failed to close fd: {}", magic_enum::enum_name(ret.error()));
            }
        }
        wlocked->clear();
    }

    thread *this_thread()
    {
        return percpu.read<thread *, &percpu::running_thread>();
    }

    std::size_t sleep(std::size_t ms)
    {
        this_thread()->prepare_sleep(ms);
        return yield();
    }

    void schedule(cpu::registers *regs);
    std::size_t yield()
    {
        auto thread = this_thread();

        const bool eeping = thread->status == status::sleeping;
        const bool old = eeping ? thread->sleep_ints : arch::int_switch_status(false);

        // do not insert sleeping threads without timeout in the sleep queue
        // moving them back into the run queue is handled in thread::wake_up
        if (eeping && thread->sleep_for.has_value())
        {
            const auto timer = chrono::main_timer();
            thread->sleep_until = timer->ns() + thread->sleep_for.value();
            thread->sleep_for = std::nullopt;

            disable();
            percpu.unsafe_get().sleep_queue.lock()->insert(thread);
            enable();
        }

        schedule(nullptr);
        arch::int_switch(old);

        return eeping ? thread->wake_reason : wake_reason::success;
    }

    [[noreturn]]
    void exit(int status)
    {
        auto current = this_thread();
        auto proc = current->parent;
        if (proc->pid == 1)
            lib::panic("attempted to kill init");

        proc->fdt.reset();

        {
            const std::unique_lock _ { proc->lock };
            proc->exit_status = status;
            proc->exited = true;

            for (auto &[tid, thrd] : proc->threads)
            {
                if (thrd == current)
                    continue;
                thrd->status = status::killed;
            }
        }

        if (!proc->children.empty())
        {
            auto init = proc_for(1);
            lib::bug_on(!init);

            const std::unique_lock _ { init->lock };
            for (auto &[pid, child] : proc->children)
            {
                child->parent = init;
                init->children[pid] = child;
            }
            proc->children.clear();
        }

        if (current->clear_child_tid)
        {
            auto ptr = reinterpret_cast<int __user *>(current->clear_child_tid);
            int zero = 0;
            lib::copy_to_user(ptr, &zero, sizeof(int));
            // TODO: futex wake on clear_child_tid address
        }

        arch::int_switch(false);
        current->status = status::killed;
        schedule(nullptr);
        std::unreachable();
    }

    std::size_t allocate_cpu()
    {
        std::size_t idx = 0;
        std::size_t min = std::numeric_limits<std::size_t>::max();

        for (std::size_t i = 0; i < cpu::count(); i++)
        {
            auto &obj = percpu.unsafe_get(cpu::local::nth_base(i));
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
        auto &pcpu = percpu.unsafe_get(cpu::local::nth_base(cpu_idx));
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
                pcpu.queue.lock()->insert(thread);
                if (!pcpu.in_scheduler.load(std::memory_order_acquire) &&
                     pcpu.idling.load(std::memory_order_acquire))
                {
                    pcpu.idling.store(false, std::memory_order_release);
                    arch::reschedule_other(cpu_idx);
                }
                break;
            default:
                std::unreachable();
        }
    }

    namespace
    {
        void reaper()
        {
            auto &pcpu = percpu.unsafe_get();
            auto &dead = pcpu.dead_threads;
            auto &me = pcpu.reaper_thread;

            while (true)
            {
                while (true)
                {
                    if (!dead.empty())
                        break;

                    me->prepare_sleep();
                    lib::bug_on(me->sleep_until.has_value());
                    yield();
                    lib::bug_on(dead.empty());
                }

                // for batch reaping
                me->prepare_sleep(timeslice * 5);
                yield();

                disable();
                auto list = std::move(dead);
                enable();

                lib::map::flat_hash<pid_t, process *> dying_procs;
                while (!list.empty())
                {
                    const auto thread = list.front();
                    list.pop_front();

                    auto proc = thread->parent;
                    bool last_thread = false;
                    {
                        const std::unique_lock _ { proc->lock };
                        proc->threads.erase(thread->tid);
                        last_thread = proc->threads.empty() && proc->exited;
                    }

                    delete thread;

                    if (last_thread && proc->pid > 0)
                        dying_procs[proc->pid] = proc;
                }

                for (auto &[pid, proc] : dying_procs)
                {
                    if (proc->parent)
                        proc->parent->waiter.signal_all();

                    // TODO: send SIGCHLD to parent
                }
            }
            std::unreachable();
        }

        void sleeper()
        {
            auto &pcpu = percpu.unsafe_get();
            auto &queue = pcpu.queue;
            auto &eepers = pcpu.sleep_queue;
            auto &dead = pcpu.dead_threads;
            auto &me = pcpu.sleeper_thread;

            while (true)
            {
                auto elocked = eepers.lock();
                while (elocked->empty())
                {
                    me->prepare_sleep();
                    lib::bug_on(me->sleep_until.has_value());
                    elocked.unlock();
                    yield();
                    elocked.lock();
                }

                const auto timer = chrono::main_timer();
                const auto time = timer->ns();

                const auto begin = elocked->begin();
                for (auto it = begin; it != elocked->end(); )
                {
                    const auto thread = (it++).value();
                    lib::bug_on(!thread->sleep_until.has_value());

                    switch (thread->status)
                    {
                        case status::ready:
                            thread->sleep_until = std::nullopt;
                            elocked->remove(thread);
                            queue.lock()->insert(thread);
                            break;
                        case status::killed:
                            thread->sleep_until = std::nullopt;
                            elocked->remove(thread);
                            dead.push_back(thread);
                            break;
                        case status::sleeping:
                        {
                            if (time < thread->sleep_until.value())
                                continue;

                            elocked->remove(thread);

                            thread->sleep_until = std::nullopt;
                            thread->status = status::ready;
                            thread->wake_reason = wake_reason::success;
                            {
                                // if the woken one has been sleeping for too long
                                auto qlocked = queue.lock();
                                if (!qlocked->empty())
                                    thread->vruntime = qlocked->first()->vruntime;
                                qlocked->insert(thread);
                            }
                            break;
                        }
                        case status::not_ready:
                        case status::running:
                            lib::panic(
                                "found a {} thread in sleep queue",
                                magic_enum::enum_name(thread->status)
                            );
                            std::unreachable();
                        default:
                            std::unreachable();
                    }
                }
                elocked.unlock();
                yield();
            }
            std::unreachable();
        }
    } // namespace

    void schedule(cpu::registers *regs)
    {
        auto &pcpu = percpu.unsafe_get();
        const auto current = pcpu.running_thread;

        if (current && current->preemption != 0)
        {
            lib::bug_on(!regs);
            arch::reschedule(timeslice);
            return;
        }
        pcpu.in_scheduler.store(true, std::memory_order_release);

        const auto timer = chrono::main_timer();
        auto time = timer->ns();

        const auto &self = cpu::self().unsafe_get();
        auto &dead = pcpu.dead_threads;

        const bool is_current_idle = (current == pcpu.idle_thread);

        if (current && !is_current_idle && current->status != status::killed)
        {
            static constexpr std::size_t weight0 = prio_to_weight(0);
            const std::size_t exec_time = time - current->schedule_time;
            const std::size_t weight = prio_to_weight(current->priority);
            current->vruntime += (exec_time * weight0) / weight;
        }

        const auto &eeper = pcpu.sleeper_thread;
        auto &eepers = pcpu.sleep_queue;
        std::size_t earliest_wake = 0;
        {
            auto elocked = eepers.lock();
            if (eeper->status == status::sleeping && !elocked->empty())
            {
                const auto first_eeper = elocked->first();
                lib::bug_on(!first_eeper->sleep_until.has_value());
                if (time >= *first_eeper->sleep_until)
                {
                    eeper->sleep_lock.unlock();
                    eeper->status = status::ready;
                    eeper->wake_reason = wake_reason::success;
                    enqueue(eeper, self.idx);
                }
                else earliest_wake = *first_eeper->sleep_until;
            }
        }

        thread *next = nullptr;
        bool found_dead = false;
        {
            auto locked = pcpu.queue.lock();

            for (auto it = locked->begin(); it != locked->end(); )
            {
                const auto thread = (it++).value();
                if (thread->status == status::ready)
                {
                    if (current && !is_current_idle &&
                        current->status == status::running &&
                        current->vruntime <= thread->vruntime)
                    {
                        next = current;
                    }
                    else
                    {
                        next = thread;
                        locked->remove(thread);
                    }
                    break;
                }
                else if (thread->status == status::killed)
                {
                    locked->remove(thread);
                    dead.push_back(thread);
                    found_dead = true;
                }
                else
                {
                    lib::panic(
                        "found a {} thread in scheduler queue",
                        magic_enum::enum_name(thread->status)
                    );
                }
            }
        }

        std::optional<pid_t> prev_pid;
        if (current != nullptr) [[likely]]
        {
            if (current->status == status::killed)
            {
                dead.push_back(current);
                found_dead = true;
            }
            else
            {
                prev_pid = current->parent->pid;
                if (!is_current_idle) [[likely]]
                {
                    if (next == nullptr && current->status == status::running)
                        next = current;
                    else if (current->status == status::sleeping)
                        current->sleep_lock.unlock();

                    if (next != current) [[likely]]
                    {
                        save(current, regs);
                        if (current->status != status::sleeping)
                            enqueue(current, self.idx);
                    }
                }
            }
        }
        if (found_dead)
        {
            auto &reaper = pcpu.reaper_thread;
            // if the reaper is sleeping and not waiting for a timeout
            if (reaper->status == status::sleeping && !reaper->sleep_until.has_value())
            {
                reaper->sleep_lock.unlock();
                reaper->status = status::ready;
                reaper->wake_reason = wake_reason::success;

                auto qlocked = pcpu.queue.lock();
                if (!qlocked->empty())
                    reaper->vruntime = qlocked->first()->vruntime;

                if (next == nullptr)
                    next = reaper;
                else
                    qlocked->insert(reaper);
            }
        }

        if (next == nullptr)
            next = pcpu.idle_thread;

        if (next != current)
        {
            next->running_on = self.self;
            next->status = status::running;
            pcpu.running_thread = next;

            const bool same_pid = (prev_pid.has_value() && *prev_pid == next->parent->pid);
            load(same_pid, next, regs);
        }

        time = timer->ns();
        if (next != pcpu.idle_thread)
        {
            next->schedule_time = time;
            arch::reschedule(timeslice);
        }
        else // don't tick on idle cpus
        {
            pcpu.idling.store(true, std::memory_order_release);
            if (!pcpu.queue.lock()->empty())
            {
                pcpu.idling.store(false, std::memory_order_release);
                arch::reschedule(timeslice);
            }
            else if (earliest_wake > time)
            {
                arch::reschedule(std::min(
                    (earliest_wake - time) / 1'000'000,
                    max_wait
                ));
            }
        }

        pcpu.in_scheduler.store(false, std::memory_order_release);

        if (!regs)
            arch::ctx_switch(current, next);
    }

    process *create_pid1(std::shared_ptr<vmm::pagemap> pmap)
    {
        static bool created = false;
        lib::bug_on(created == true);
        created = true;

        auto proc = new process { };
        proc->pid = 1;
        processes.write_lock().value()[proc->pid] = proc;
        proc->vmspace = std::make_shared<vmm::vmspace>(std::move(pmap));

        auto grp = new group { };
        grp->pgid = proc->pgid = proc->pid;
        grp->members.write_lock().value()[proc->pid] = proc;
        groups.write_lock().value()[grp->pgid] = grp;

        auto sess = new session { };
        proc->sid = grp->sid = sess->sid = grp->pgid;
        sess->members.write_lock().value()[grp->pgid] = grp;
        sessions.write_lock().value()[sess->sid] = sess;

        proc->fdt = std::make_shared<process::fdtable>();
        proc->vfs = std::make_shared<process::vfs_state>();
        proc->vfs->root = proc->vfs->cwd = vfs::get_root(true);

        return proc;
    }

    lib::initgraph::stage *pid0_created_stage()
    {
        static lib::initgraph::stage stage
        {
            "sched.pid0.created",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task pid0_task
    {
        "sched.pid0.create",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            arch::cpus_stage(),
            timers::initialised_stage()
        },
        lib::initgraph::entail { pid0_created_stage() },
        [] {
            lib::bug_on(kvmspace == nullptr);

            pid0 = new process { };
            pid0->pid = 0;
            processes.write_lock().value()[pid0->pid] = pid0;

            pid0->vmspace = kvmspace;

            pid0->vfs = std::make_shared<process::vfs_state>();
            pid0->vfs->root = pid0->vfs->cwd = vfs::get_root(true);
        }
    };

    void enable()
    {
        if (!initialised)
            return;

        arch::disable();

        auto &pcpu_ref = percpu.unsafe_get();
        if (pcpu_ref.reaper_thread->preemption < 0 ||
            pcpu_ref.in_scheduler.load(std::memory_order_acquire))
        {
            arch::enable();
            return;
        }

        arch::enable();
        arch::enable();
    }

    void disable()
    {
        if (!initialised)
            return;

        arch::disable();
        if (percpu.unsafe_get().in_scheduler.load(std::memory_order_acquire))
            arch::enable();
    }

    [[noreturn]] void start()
    {
        static std::atomic_bool should_start = false;

        const auto &self = cpu::self().unsafe_get();

        const auto idle_proc = new process { };
        idle_proc->pid = -1;
        {
            const std::unique_lock _ { kvmspace_lock };
            if (kvmspace == nullptr)
            {
                kvmspace = std::make_shared<vmm::vmspace>(
                    std::make_shared<vmm::pagemap>(vmm::kernel_pagemap.get())
                );
            }
            idle_proc->vmspace = kvmspace;
        }

        const auto idle_thread = thread::create(idle_proc, reinterpret_cast<std::uintptr_t>(idle), 0, false);
        idle_thread->status = status::ready;

        auto &pcpu = percpu.unsafe_get();
        pcpu.idle_proc = idle_proc;
        pcpu.idle_thread = idle_thread;

        arch::init();

        if (self.idx == cpu::bsp_idx())
        {
            for (std::size_t idx = 0; idx < cpu::count(); idx++)
            {
                auto &obj = percpu.unsafe_get(cpu::local::nth_base(idx));
                obj.reaper_thread = sched::spawn_on(idx, reinterpret_cast<std::uintptr_t>(reaper), 0, nice_t::max);
                obj.reaper_thread->can_migrate = false;
                obj.sleeper_thread = sched::spawn_on(idx, reinterpret_cast<std::uintptr_t>(sleeper), 0, -5);
                obj.sleeper_thread->can_migrate = false;
            }

            initialised.store(true, std::memory_order_release);
            should_start.store(true, std::memory_order_release);
        }

        while (!should_start.load(std::memory_order_acquire))
            arch::pause();

        arch::reschedule(0);
        arch::halt(true);
    }
} // namespace sched
