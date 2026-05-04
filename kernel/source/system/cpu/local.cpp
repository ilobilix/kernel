// Copyright (C) 2024-2026  ilobilo

module system.cpu.local;

import system.sched;
import system.cpu;
import system.memory;
import boot;
import lib;
import std;

namespace cpu
{
    namespace local
    {
        namespace
        {
            [[gnu::section(".percpu_head")]]
            local::storage<processor> me;

            std::uintptr_t *bases;
        } // namespace

        extern "C"
        {
            extern void (*__start_percpu_init[])(std::uintptr_t);
            extern void (*__end_percpu_init[])(std::uintptr_t);

            extern char __start_percpu[], __end_percpu[];
        } // extern "C"

        std::uintptr_t map()
        {
            static std::size_t offset = 0;

            const auto base = reinterpret_cast<std::uintptr_t>(__end_percpu) + offset;
            const std::size_t size =
                reinterpret_cast<std::uintptr_t>(__end_percpu) -
                reinterpret_cast<std::uintptr_t>(__start_percpu);

            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);
            const auto flags = vmm::pflag::rwg;

            for (std::size_t offset = 0; offset < size; offset += npsize)
            {
                const auto paddr = pmm::alloc(npsize / pmm::page_size, true);
                const auto ret = vmm::kernel_pagemap->map(
                    base + offset, paddr,
                    npsize, flags, psize,
                    vmm::caching::normal
                );
                if (!ret)
                {
                    lib::panic(
                        "cpu: could not map percpu area: {}",
                        lib::error_name(ret.error())
                    );
                }
            }

            for (auto func = __start_percpu_init; func < __end_percpu_init; func++)
                (*func)(base);

            offset += size;
            return base;
        }

        processor *request(std::size_t aid)
        {
            static std::size_t next = 0;
            const std::size_t idx = next++;

            if (idx == 0)
            {
                lib::info("cpu: initialising bsp");

                if (!bases) [[likely]]
                    bases = new std::uintptr_t[count()] { };
            }
            else lib::info("cpu: bringing up ap {}", idx);

            const auto base = map();
            me.initialise(bases[idx] = base);

            auto proc = nth(idx);
            lib::bug_on(base != reinterpret_cast<std::uintptr_t>(proc));

            proc->self = proc;
            proc->stack_top = lib::alloc<std::uintptr_t>(boot::kstack_size) + boot::kstack_size;
            proc->idx = idx;
            proc->arch_id = aid;

            return proc;
        }

        processor *nth(std::size_t n)
        {
            lib::bug_on(n >= count() || !bases);
            return std::addressof(me.unsafe_get(bases[n]));
        }

        bool available()
        {
            if (!bases)
                return false;

            for (std::size_t i = 0; i < count(); i++)
            {
                if (bases[i] == 0)
                    return false;

                if (!me.unsafe_get(bases[i]).online.load(std::memory_order_acquire))
                    return false;
            }

            return true;
        }

        std::uintptr_t nth_base(std::size_t n)
        {
            lib::bug_on(n >= count() || !bases);
            return bases[n];
        }

        std::size_t arch2idx(std::size_t arch_id)
        {
            lib::bug_on(!bases);
            for (std::size_t i = 0; i < count(); i++)
            {
                const auto &cpu = me.unsafe_get(bases[i]);
                if (cpu.arch_id == arch_id)
                    return cpu.idx;
            }
            std::unreachable();
        }

        void begin_access()
        {
            sched::preempt_disable();
        }

        void end_access()
        {
            sched::preempt_enable();
        }
    } // namespace local

    local::storage<processor> &self()
    {
        return local::me;
    }
} // namespace cpu
