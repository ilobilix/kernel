// Copyright (C) 2022-2024  ilobilo

module system.cpu;

import x86_64.system.lapic;
import x86_64.system.cpu;
import x86_64.system.gdt;
import system.memory.virt;
import system.memory.phys;
import system.cpu.self;
import system.chrono;
import system.acpi;
import magic_enum;
import arch;
import lib;
import std;

#if !ILOBILIX_LIMINE_MP

namespace cpu::mp
{
    using namespace x86_64;

    std::size_t num_cores()
    {
        static const auto cached = []
        {
            const bool x2apic = x86_64::apic::supported().second;

            std::size_t max_cpus = 0;
            {
                for (const auto entry : acpi::madt::lapics)
                {
                    if ((entry.flags & 1) ^ ((entry.flags >> 1) & 1))
                        max_cpus++;
                }
                if (x2apic)
                {
                    for (const auto entry : acpi::madt::x2apics)
                    {
                        if ((entry.flags & 1) ^ ((entry.flags >> 1) & 1))
                            max_cpus++;
                    }
                }
            }
            return max_cpus;
        } ();
        return cached;
    }

    std::size_t bsp_aid()
    {
        // called from bsp
        static const auto cached = []
        {
            const bool x2apic = x86_64::apic::supported().second;
            const auto val = apic::read(apic::reg::id);
            return x2apic ? val : val >> 24;
        } ();
        return cached;
    }

    extern "C" char smp_trampoline_start[];
    extern "C" std::size_t smp_trampoline_size;

    extern "C" bool pagemap_use_lowmem;

    void boot_cores(processor *(*request)(std::size_t))
    {
        if (num_cores() <= 1)
            return;

        lib::bug_on(smp_trampoline_size > pmm::page_size);

        const auto trampoline_page = pmm::alloc<std::uintptr_t>(1, false, pmm::type::sub1mib);
        const auto temp_stack_page = pmm::alloc<std::uintptr_t>(1, false, pmm::type::sub1mib);

        pagemap_use_lowmem = true;
            vmm::pagemap temp_map { };
        pagemap_use_lowmem = false;

        const auto map = [&temp_map]<typename ...Args>(std::string_view str, Args &&...args)
        {
            auto inner = [&](auto &obj) {
                if (const auto ret = obj.map(std::forward<Args>(args)...); !ret)
                    lib::panic("could not map {}: {}", str, magic_enum::enum_name(ret.error()));
            };
            pagemap_use_lowmem = true;
                inner(temp_map);
            pagemap_use_lowmem = false;
            inner(*vmm::kernel_pagemap);
        };

        const auto unmap = [&temp_map]<typename ...Args>(std::string_view str, Args &&...args)
        {
            auto inner = [&](auto &obj) {
                if (const auto ret = obj.unmap(std::forward<Args>(args)...); !ret)
                    lib::panic("could not unmap {}: {}", str, magic_enum::enum_name(ret.error()));
            };
            inner(temp_map);
            inner(*vmm::kernel_pagemap);
        };

        lib::debug("cpu: trampoline address 0x{:X}", trampoline_page);
        map("trampoline address", trampoline_page, trampoline_page, smp_trampoline_size, vmm::pflag::rwx);
        std::memcpy(reinterpret_cast<void *>(trampoline_page), smp_trampoline_start, smp_trampoline_size);

        lib::debug("cpu: temporary stack address 0x{:X}", temp_stack_page);
        map("temporary stack", temp_stack_page, temp_stack_page, pmm::page_size, vmm::pflag::rwx);

        const bool x2apic = x86_64::apic::supported().second;
        mtrr::save();

        auto start_ap = [&](std::size_t lapic_id)
        {
            if (lapic_id == bsp_aid())
                return;

            struct trampoline_info
            {
                std::uint64_t booted_flag;
                std::uint64_t early_pagemap;
                std::uint64_t pagemap;
                std::uint64_t bsp_apic_addr;
                std::uint64_t mtrr_restore;
                std::uint64_t temp_stack;
                std::uint64_t proc;
                std::uint64_t jump_addr;
            };

            const auto cpu = request(lapic_id);

            volatile auto info = reinterpret_cast<trampoline_info *>(trampoline_page + smp_trampoline_size - sizeof(trampoline_info));
            *info = trampoline_info {
                .booted_flag = 0,
                .early_pagemap = reinterpret_cast<std::uint64_t>(temp_map.get_arch_table()),
                .pagemap = reinterpret_cast<std::uint64_t>(vmm::kernel_pagemap->get_arch_table()),
                .bsp_apic_addr = msr::read(0x1B),
                .mtrr_restore = reinterpret_cast<std::uint64_t>(mtrr::restore),
                .temp_stack = temp_stack_page + pmm::page_size,
                .proc = reinterpret_cast<std::uint64_t>(cpu),
                .jump_addr = reinterpret_cast<std::uint64_t>(arch::core::entry)
            };

            apic::ipi(lapic_id, apic::destination::physical, apic::delivery::init, 0);
            chrono::stall_ns(10'000'000);

            apic::ipi(lapic_id, apic::destination::physical, apic::delivery::startup, (trampoline_page >> 12));
            chrono::stall_ns(200'000);
            apic::ipi(lapic_id, apic::destination::physical, apic::delivery::startup, (trampoline_page >> 12));
            chrono::stall_ns(200'000);

            for (std::size_t i = 0; i < 1000; i++)
            {
                if (__atomic_load_n(&info->booted_flag, __ATOMIC_RELAXED) == 1)
                    return;
                chrono::stall_ns(300'000);
            }
            lib::panic("could not boot up a core");
        };

        for (const auto entry : acpi::madt::lapics)
        {
            if (!((entry.flags & 1) ^ ((entry.flags >> 1) & 1)))
                continue;

            start_ap(entry.id);
        }
        if (x2apic)
        {
            for (const auto entry : acpi::madt::x2apics)
            {
                if (!((entry.flags & 1) ^ ((entry.flags >> 1) & 1)))
                    continue;

                start_ap(entry.id);
            }
        }

        [] {
            lib::bitmap bmap { count() - 1 };
            bmap.clear();

            const auto check = [&bmap] {
                bool ret = true;
                for (std::size_t i = 1; i < count(); i++)
                {
                    if (nth(i)->online == false)
                        ret = false;
                    else if (bmap.get(i - 1) == false)
                        bmap.set(i - 1, true);
                }
                return ret;
            };

            for (std::size_t i = 0; i < 100'000; i++)
            {
                if (check())
                    return;
                chrono::stall_ns(300'000);
            }
            lib::panic("could not boot up cores");
        } ();

        unmap("trampoline address", trampoline_page, smp_trampoline_size);
        unmap("temporary stack", temp_stack_page, pmm::page_size);
    }
} // export namespace cpu::mp

#endif