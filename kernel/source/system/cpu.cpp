// Copyright (C) 2024-2025  ilobilo

module system.cpu;

import system.cpu.self;
import system.memory;
import system.time;
import magic_enum;
import boot;
import arch;
import lib;
import std;

namespace cpu
{
    namespace
    {
        constexpr std::size_t _bsp_idx = 0;
        std::size_t _bsp_aid;
    } // namespace

    std::size_t bsp_idx() { return _bsp_idx; }
    std::size_t bsp_aid() { return _bsp_aid; }

    namespace local
    {
        extern "C"
        {
            extern void (*__start_percpu_init[])(std::uintptr_t);
            extern void (*__end_percpu_init[])(std::uintptr_t);

            extern char __start_percpu[], __end_percpu[];
        } // extern "C"

        namespace
        {
            [[gnu::section(".percpu_head")]]
            local::storage<processor> me;

            std::uintptr_t *bases;

            bool _available = false;
        } // namespace

        extern "C++" bool available() { return _available; }

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
                log::info("cpu: initialising bsp");
            else
                log::info("cpu: bringing up ap {}", idx);

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
    } // namespace local

    extern "C++" processor *self()
    {
        return local::bases ? std::addressof(local::me.get()) : nullptr;
    }

#if ILOBILIX_LIMINE_MP
    extern "C" void mp_entry(boot::limine_mp_info *);
    extern "C" void generic_mp_entry(boot::limine_mp_info *info)
    {
        const auto args = reinterpret_cast<std::uintptr_t *>(info->extra_argument);
        arch::core::entry(args[1]);
    }
#else
    namespace mp
    {
        std::size_t num_cores();
        std::size_t bsp_aid();

        void boot_cores(processor *(*request)(std::size_t));
    } // namespace mp
#endif

    std::size_t count()
    {
#if ILOBILIX_LIMINE_MP
        static const auto cached = [] { return boot::requests::mp.response->cpu_count; } ();
        return cached;
#else
        return mp::num_cores();
#endif
    }

    void init_bsp()
    {
        local::bases = new std::uintptr_t[count()] { };

#if ILOBILIX_LIMINE_MP
#  if defined(__x86_64__)
        _bsp_aid = boot::requests::mp.response->bsp_lapic_id;
#  elif defined(__aarch64__)
        _bsp_aid = boot::requests::mp.response->bsp_mpidr;
#  endif
#else
        _bsp_aid = mp::bsp_aid();
#endif

        const auto proc = local::request(_bsp_aid);
        arch::core::bsp(reinterpret_cast<std::uintptr_t>(proc));
    }

    void init()
    {
        log::info("cpu: number of available processors: {}", count());
#if ILOBILIX_LIMINE_MP
        for (std::size_t i = 0; i < count(); i++)
        {
            const auto entry = boot::requests::mp.response->cpus[i];
#  if defined(__x86_64__)
            const auto aid = entry->lapic_id;
#  elif defined(__aarch64__)
            const auto aid = entry->mpidr;
#  endif
            if (aid == bsp_aid())
                continue;

            const auto cpu = local::request(aid);
            std::uintptr_t args[2] {
                reinterpret_cast<std::uintptr_t>(vmm::kernel_pagemap->get_arch_table()),
                reinterpret_cast<std::uintptr_t>(cpu)
            };
            entry->extra_argument = reinterpret_cast<std::uint64_t>(&args);
            __atomic_store_n(&entry->goto_address, mp_entry, __ATOMIC_SEQ_CST);

            for (std::size_t i = 0; i < 100'000; i++)
            {
                if (cpu->online)
                    goto next;
                time::stall_ns(300'000);
            }
            lib::panic("could not boot up a core");
            next:
        }
#else
        mp::boot_cores(local::request);
#endif
    }
} // namespace cpu