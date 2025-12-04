// Copyright (C) 2024-2025  ilobilo

module system.cpu.local;

import system.cpu;
import system.memory;
import magic_enum;
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

            bool _available = false;
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
            const auto flags = vmm::pflag::rwg;

            if (const auto ret = vmm::kernel_pagemap->map_alloc(base, size, flags, psize); !ret)
                lib::panic("could not map percpu data: {}", magic_enum::enum_name(ret.error()));

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
            me.initialise_base(bases[idx] = base);

            auto proc = nth(idx);
            lib::bug_on(base != reinterpret_cast<std::uintptr_t>(proc));

            proc->self = proc;
            proc->idx = idx;
            proc->arch_id = aid;
            proc->stack_top = lib::alloc<std::uintptr_t>(boot::kstack_size) + boot::kstack_size;

            _available = true;
            return proc;
        }

        processor *nth(std::size_t n)
        {
            return bases ? std::addressof(me.get(bases[n])) : nullptr;
        }

        std::uintptr_t nth_base(std::size_t n)
        {
            return bases ? bases[n] : 0;
        }

        bool available()
        {
            return _available;
        }
    } // namespace local

    processor *self()
    {
        return local::bases ? std::addressof(local::me.get()) : nullptr;
    }
} // namespace cpu