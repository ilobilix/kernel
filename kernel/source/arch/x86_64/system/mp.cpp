// Copyright (C) 2022-2024  ilobilo

module system.cpu;

import x86_64.system.lapic;
import x86_64.system.cpu;
import x86_64.system.gdt;
import system.memory.virt;
import system.memory.phys;
import system.cpu.self;
import system.time;
import system.acpi;
import magic_enum;
import arch;
import lib;
import cppstd;

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
    extern "C" std::uintptr_t trampoline_pages;

    void boot_cores(processor *(*request)(std::size_t))
    {
        if (num_cores() <= 1)
            return;

        lib::bug_if_not(smp_trampoline_size <= pmm::page_size);
        lib::bug_if_not(trampoline_pages != 0);

        log::debug("cpu: trampoline address 0x{:X}", trampoline_pages);
        if (const auto ret = vmm::kernel_pagemap->map(trampoline_pages, trampoline_pages, smp_trampoline_size, vmm::pflag::rwx); !ret)
            lib::panic("could not map trampoline address: {}", magic_enum::enum_name(ret.error()));
        std::memcpy(reinterpret_cast<void *>(trampoline_pages), smp_trampoline_start, smp_trampoline_size);

        const auto temp_stack = trampoline_pages + pmm::page_size;
        log::debug("cpu: temporary stack address 0x{:X}", temp_stack);
        if (const auto ret = vmm::kernel_pagemap->map(temp_stack, temp_stack, pmm::page_size, vmm::pflag::rwx); !ret)
            lib::panic("could not map temporary stack: {}", magic_enum::enum_name(ret.error()));

        const bool x2apic = x86_64::apic::supported().second;
        mtrr::save();

        auto start_ap = [&request, temp_stack](std::size_t lapic_id)
        {
            if (lapic_id == bsp_aid())
                return;

            struct trampoline_info
            {
                std::uint64_t booted_flag;
                std::uint64_t pagemap;
                std::uint64_t bsp_apic_addr;
                std::uint64_t mtrr_restore;
                std::uint64_t temp_stack;
                std::uint64_t proc;
                std::uint64_t jump_addr;
            };

            const auto cpu = request(lapic_id);

            volatile auto info = reinterpret_cast<trampoline_info *>(trampoline_pages + smp_trampoline_size - sizeof(trampoline_info));
            *info = trampoline_info {
                .booted_flag = 0,
                .pagemap = reinterpret_cast<std::uint64_t>(vmm::kernel_pagemap->get_arch_table()),
                .bsp_apic_addr = msr::read(0x1B),
                .mtrr_restore = reinterpret_cast<std::uint64_t>(mtrr::restore),
                .temp_stack = temp_stack + pmm::page_size,
                .proc = reinterpret_cast<std::uint64_t>(cpu),
                .jump_addr = reinterpret_cast<std::uint64_t>(arch::core::entry)
            };

            apic::ipi(lapic_id, apic::destination::physical, apic::delivery::init, 0);
            time::stall_ns(10'000'000);

            apic::ipi(lapic_id, apic::destination::physical, apic::delivery::startup, (trampoline_pages >> 12));
            time::stall_ns(200'000);
            apic::ipi(lapic_id, apic::destination::physical, apic::delivery::startup, (trampoline_pages >> 12));
            time::stall_ns(200'000);

            for (std::size_t i = 0; i < 1000; i++)
            {
                if (__atomic_load_n(&info->booted_flag, __ATOMIC_RELAXED) == 1)
                    return;
                time::stall_ns(300'000);
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
            lib::bitmap bmap { cpu_count() - 1 };
            bmap.clear();

            const auto check = [&bmap] {
                bool ret = true;
                for (std::size_t i = 1; i < cpu_count(); i++)
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
                time::stall_ns(300'000);
            }
            lib::panic("could not boot up cores");
        } ();

        if (const auto ret = vmm::kernel_pagemap->unmap(trampoline_pages, smp_trampoline_size); !ret)
            lib::panic("could not unmap trampoline address: {}", magic_enum::enum_name(ret.error()));

        if (const auto ret = vmm::kernel_pagemap->unmap(temp_stack, pmm::page_size); !ret)
            lib::panic("could not unmap temporary stack: {}", magic_enum::enum_name(ret.error()));
    }
} // export namespace cpu::mp