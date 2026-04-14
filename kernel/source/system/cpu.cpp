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
#if defined(__x86_64__)
            auto get_arch_id(auto *cpu) { return cpu->lapic_id; }
            auto get_bsp_id(auto *smp) { return smp->bsp_lapic_id; }
#elif defined(__aarch64__)
            auto get_arch_id(auto *cpu) { return cpu->mpidr; }
            auto get_bsp_id(auto *smp) { return smp->bsp_mpidr; }
#else
#  error Unsupported architecture
#endif

        [[gnu::section(".percpu_head")]]
        per::storage<processor> me;

        std::uintptr_t *bases;

        std::size_t _bsp_idx;
        std::size_t _bsp_aid;
        std::size_t _cpu_count = 0;
    } // namespace

    std::size_t bsp_idx() { return _bsp_idx; }
    std::size_t bsp_aid() { return _bsp_aid; }
    std::size_t cpu_count() { return _cpu_count; }

    namespace per
    {
        extern "C" void (*__percpu_init_start[])(std::uintptr_t);
        extern "C" void (*__percpu_init_end[])(std::uintptr_t);

        extern "C" char __percpu_start[];
        extern "C" char __percpu_end[];

        extern "C++" std::uintptr_t init()
        {
            static std::size_t offset = 0;

            const auto base = reinterpret_cast<std::uintptr_t>(__percpu_end) + offset;
            const std::size_t size =
                reinterpret_cast<std::uintptr_t>(__percpu_end) -
                reinterpret_cast<std::uintptr_t>(__percpu_start);

            const auto flags = vmm::flag::rw;
            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);
            const auto npages = lib::div_roundup(npsize, pmm::page_size);

            for (std::size_t i = 0; i < size; i += npsize)
            {
                const auto paddr = pmm::alloc<std::uintptr_t>(npages);
                if (auto ret = vmm::kernel_pagemap->map(base + i, paddr, npsize, flags, psize); !ret)
                    lib::panic("could not map percpu data: {}", magic_enum::enum_name(ret.error()));
            }

            for (auto func = __percpu_init_start; func < __percpu_init_end; func++)
                (*func)(base);

            offset += size;
            return base;
        }
    } // namespace per

    processor *nth(std::size_t n)
    {
        return bases ? std::addressof(me.get(bases[n])) : nullptr;
    }

    std::uintptr_t nth_base(std::size_t n)
    {
        return bases ? bases[n] : 0;
    }

    extern "C++" processor *self()
    {
        return bases ? std::addressof(me.get()) : nullptr;
    }

    extern "C" std::uint8_t kernel_stack[];
    void init_bsp()
    {
        const auto smp = boot::requests::smp.response;

        bases = new std::uintptr_t[smp->cpu_count] { };
        _bsp_aid = get_bsp_id(smp);

        for (std::size_t i = 0; i < smp->cpu_count; i++)
        {
            auto cpu = smp->cpus[i];
            const auto aid = get_arch_id(cpu);

            if (aid != _bsp_aid)
                continue;

            log::info("cpu: {} {}: arch id: {}", "initialising bsp", i, aid);

            const auto base = per::init();
            me.initialise_base(bases[i] = base);

            auto proc = nth(i);
            lib::ensure(base == reinterpret_cast<std::uintptr_t>(proc));

            proc->self = proc;
            proc->idx = _bsp_idx = i;
            proc->arch_id = aid;

            // unnecessary
            // proc->stack_top = reinterpret_cast<std::uintptr_t>(kernel_stack) + boot::kstack_size;

            cpu->extra_argument = base;
            arch::core::bsp(cpu);
        }
    }

    struct extra_arg { std::uintptr_t pmap; std::uintptr_t pcpu; };
    // * NOTICE: we do not initialise cores in parallel so this works fine
    extra_arg earg;

    extern "C" void cpu_entry(boot::limine_mp_info *);
    extern "C" void generic_core_entry(boot::limine_mp_info *cpu)
    {
        // auto earg = reinterpret_cast<extra_arg *>(cpu->extra_argument);
        // cpu->extra_argument = earg->pcpu;
        // delete earg;
        cpu->extra_argument = earg.pcpu;
        arch::core::entry(cpu);
    }

    void init()
    {
        const auto smp = boot::requests::smp.response;
        log::info("cpu: number of available processors: {}", smp->cpu_count);

        _cpu_count = smp->cpu_count;

        for (std::size_t i = 0; i < _cpu_count; i++)
        {
            auto cpu = smp->cpus[i];
            const auto aid = get_arch_id(cpu);

            if (aid == _bsp_aid)
                continue;

            log::info("cpu: {} {}: arch id: {}", "bringing up cpu", i, aid);

            const auto base = per::init();
            me.initialise_base(bases[i] = base);

            auto proc = nth(i);
            lib::ensure(base == reinterpret_cast<std::uintptr_t>(proc));

            proc->self = proc;
            proc->idx = i;
            proc->arch_id = aid;

            const auto stack = vmm::alloc_vpages(vmm::space_type::other, boot::kstack_size / pmm::page_size);

            const auto flags = vmm::flag::rw;
            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);
            const auto npages = lib::div_roundup(npsize, pmm::page_size);

            for (std::size_t i = 0; i < boot::kstack_size; i += npsize)
            {
                const auto paddr = pmm::alloc<std::uintptr_t>(npages, true);
                if (auto ret = vmm::kernel_pagemap->map(stack + i, paddr, npsize, flags, psize); !ret)
                    lib::panic("could not map cpu {} kernel stack: {}", i, magic_enum::enum_name(ret.error()));
            }

            proc->stack_top = stack + boot::kstack_size;

            // cpu->extra_argument = reinterpret_cast<std::uintptr_t>(
            //     new extra_arg { reinterpret_cast<std::uintptr_t>(vmm::kernel_pagemap->get_arch_table()), base }
            // );

            earg.pmap = reinterpret_cast<std::uintptr_t>(vmm::kernel_pagemap->get_arch_table());
            earg.pcpu = base;

            cpu->extra_argument = reinterpret_cast<std::uintptr_t>(&earg);
            cpu->goto_address = &cpu_entry;

            while (!proc->online)
                arch::pause();
        }
    }
} // namespace cpu