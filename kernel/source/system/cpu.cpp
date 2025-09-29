// Copyright (C) 2024-2025  ilobilo

module system.cpu;

import system.cpu.self;
import system.memory;
import magic_enum;
import boot;
import arch;
import lib;
import cppstd;

namespace cpu
{
    namespace
    {
        [[gnu::section(".percpu_head")]]
        per::storage<processor> me;

        std::uintptr_t *bases;

        constexpr std::size_t _bsp_idx = 0;
        std::size_t _bsp_aid;
    } // namespace

    std::size_t bsp_idx() { return _bsp_idx; }
    std::size_t bsp_aid() { return _bsp_aid; }

    namespace per
    {
        extern "C" void (*__start_percpu_init[])(std::uintptr_t);
        extern "C" void (*__end_percpu_init[])(std::uintptr_t);

        extern "C" char __start_percpu[];
        extern "C" char __end_percpu[];

        std::uintptr_t init()
        {
            static std::size_t offset = 0;

            const auto base = reinterpret_cast<std::uintptr_t>(__end_percpu) + offset;
            const std::size_t size =
                reinterpret_cast<std::uintptr_t>(__end_percpu) -
                reinterpret_cast<std::uintptr_t>(__start_percpu);

            const auto psize = vmm::page_size::small;
            const auto flags = vmm::pflag::rw;

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
                log::info("cpu: initialising bsp: arch id: {}", aid);
            else
                log::info("cpu: bringing up ap {}", idx, aid);

            const auto base = per::init();
            me.initialise_base(bases[idx] = base);

            auto proc = nth(idx);
            lib::bug_on(base != reinterpret_cast<std::uintptr_t>(proc));

            proc->self = proc;
            proc->idx = idx;
            proc->arch_id = aid;

            const auto psize = vmm::page_size::small;
            const auto flags = vmm::pflag::rw;
            const auto size = boot::kstack_size;

            const auto stack = vmm::alloc_vpages(
                vmm::space_type::stack,
                size / pmm::page_size
            );

            if (const auto ret = vmm::kernel_pagemap->map_alloc(stack, size, flags, psize); !ret)
            {
                lib::panic("could not map cpu {} kernel stack: {}",
                    idx, magic_enum::enum_name(ret.error())
                );
            }

            proc->stack_top = stack + size;
            return proc;
        }
    } // namespace per

    processor *nth(std::size_t n) { return bases ? std::addressof(me.get(bases[n])) : nullptr; }
    std::uintptr_t nth_base(std::size_t n) { return bases ? bases[n] : 0; }

    bool percpu_available() { return bases != nullptr; }
    extern "C++" processor *self() { return bases ? std::addressof(me.get()) : nullptr; }

    namespace mp
    {
        std::size_t num_cores();
        std::size_t bsp_aid();

        void boot_cores(processor *(*request)(std::size_t));
    } // namespace mp

    std::size_t cpu_count() { return mp::num_cores(); }

    void init_bsp()
    {
        bases = new std::uintptr_t[mp::num_cores()] { };

        _bsp_aid = bsp_aid();
        const auto proc = per::request(_bsp_aid);
        arch::core::bsp(reinterpret_cast<std::uintptr_t>(proc));
    }

    void init()
    {
        log::info("cpu: number of available processors: {}", mp::num_cores());
        mp::boot_cores(per::request);
    }
} // namespace cpu