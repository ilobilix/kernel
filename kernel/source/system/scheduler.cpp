// Copyright (C) 2024-2025  ilobilo

module system.scheduler;

import system.memory;
import system.cpu.self;
import boot;
import arch;
import lib;
import std;

namespace sched
{
    namespace arch
    {
        void init();
        void reschedule(std::size_t ms);

        void finalise(std::shared_ptr<thread> thread, std::uintptr_t ip);
        void deinitialise(std::shared_ptr<thread> thread);

        void save(std::shared_ptr<thread> thread);
        void load(std::shared_ptr<thread> thread);
    } // namespace arch

    namespace
    {
        lib::map::flat_hash<std::size_t, std::shared_ptr<process>> processes;
        lib::spinlock<false> process_lock;

        std::atomic_size_t next_pid = 0;
        std::size_t alloc_pid(std::shared_ptr<process> proc)
        {
            std::unique_lock _ { process_lock };
            auto pid = next_pid++;
            processes[pid] = proc;
            return pid;
        }

        std::shared_ptr<thread> next_thread(bool idle = false)
        {
            auto &self = cpu::self()->sched;
            std::unique_lock _ { self.lock };

            auto &queue = self.queue;
            if (queue.empty() || idle)
                return self.idle_thread;

            auto thread = queue.front();
            queue.pop_front();
            return thread;
        }
        void save(std::shared_ptr<thread> thread, cpu::registers *regs)
        {
            std::memcpy(&thread->regs, regs, sizeof(cpu::registers));
            arch::save(thread);
        }

        void load(std::shared_ptr<thread> thread, cpu::registers *regs)
        {
            std::memcpy(regs, &thread->regs, sizeof(cpu::registers));
            arch::load(thread);
            thread->proc.lock()->pagemap->load();
        }

        void idle()
        {
            ::arch::halt(true);
        }
    } // namespace

    std::uintptr_t thread::allocate_kstack()
    {
        auto paddr = pmm::alloc<std::byte *>(lib::div_roundup(boot::kernel_stack_size, pmm::page_size));
        auto vaddr = lib::tohh(paddr);
        std::memset(vaddr, 0, boot::kernel_stack_size);

        stacks.push_back(paddr);
        return reinterpret_cast<std::uintptr_t>(vaddr) + boot::kernel_stack_size;
    }

    std::shared_ptr<thread> thread::create(std::shared_ptr<process> parent, std::uintptr_t ip)
    {
        auto thread = std::make_shared<sched::thread>();

        thread->tid = parent->next_tid++;
        thread->proc = parent;
        thread->status = status::not_ready;
        thread->is_user = false;

        auto stack = thread->allocate_kstack();
        thread->kstack_top = thread->ustack_top = stack;
        arch::finalise(thread, ip);

        std::unique_lock _ { parent->lock };
        parent->threads[thread->tid] = thread;
        // enqueued manually

        return thread;
    }

    thread::~thread()
    {
        for (auto &stack : stacks)
            pmm::free(stack, lib::div_roundup(boot::kernel_stack_size, pmm::page_size));

        // TODO
    }

    std::shared_ptr<process> process::create(std::shared_ptr<process> parent, std::shared_ptr<vmm::pagemap> pagemap)
    {
        auto proc = std::make_shared<process>();
        proc->pid = alloc_pid(proc);
        proc->root = proc->cwd = vfs::node::root();
        proc->pagemap = pagemap;

        if (parent)
        {
            proc->parent = parent;
            std::unique_lock _ { parent->lock };
            parent->children[proc->pid] = proc;
        }
        return proc;
    }

    process::~process()
    {
        // TODO
    }

    std::size_t allocate_cpu()
    {
        std::size_t idx = 0;
        std::size_t min = std::numeric_limits<std::size_t>::max();

        for (std::size_t i = 0; i < cpu::cpu_count; i++)
        {
            auto &sched = cpu::processors[i].sched;
            auto size = sched.queue.size();
            if (size < min)
            {
                min = size;
                idx = i;
            }
        }
        return idx;
    }

    void enqueue(std::shared_ptr<thread> thread, std::size_t cpu_idx)
    {
        if (thread->status == status::running)
            thread->status = status::ready;

        auto &sched = cpu::processors[cpu_idx].sched;
        std::unique_lock _ { sched.lock };
        sched.queue.push_back(thread);
    }

    void schedule(cpu::registers *regs)
    {
        auto self = cpu::self();
        auto &sched = self->sched;

        auto first = next_thread();
        auto next = first;
        while (next->status != status::ready)
        {
            next = next_thread();
            if (next == first)
                next = sched.idle_thread;
        }

        auto current = sched.running_thread;
        if (current && current->status != status::killed)
        {
            save(current, regs);
            if (current != sched.idle_thread)
                enqueue(current, self->idx);
        }
        sched.running_thread = next;

        arch::reschedule(fixed_timeslice);
        load(next, regs);
    }

    [[noreturn]] void start()
    {
        static auto idle_pagemap = std::make_shared<vmm::pagemap>(vmm::kernel_pagemap.get());
        auto idle_proc = std::make_shared<process>();
        idle_proc->pagemap = idle_pagemap;

        auto idle_thread = thread::create(idle_proc, reinterpret_cast<std::uintptr_t>(idle));
        idle_thread->status = status::ready;

        cpu::self()->sched.idle_proc = idle_proc;
        cpu::self()->sched.idle_thread = idle_thread;

        arch::init();
        ::arch::int_switch(true);
        arch::reschedule(0);
        ::arch::halt(true);
    }
} // namespace sched