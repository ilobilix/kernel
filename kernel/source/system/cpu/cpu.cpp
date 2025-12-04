// Copyright (C) 2024-2025  ilobilo

module system.cpu;

import system.cpu.local;
import system.memory;
import system.chrono;
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
        lib::info("cpu: number of available processors: {}", count());
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
                chrono::stall_ns(300'000);
            }
            lib::panic("could not boot up a core");
            next:
        }
#else
        mp::boot_cores(local::request);
#endif
    }
} // namespace cpu