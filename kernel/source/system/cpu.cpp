// Copyright (C) 2024-2025  ilobilo

module system.cpu;

import system.cpu.self;
import boot;
import arch;
import lib;
import std;

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
    } // namespace

    extern "C++" processor *self()
    {
        if (processors == nullptr) [[unlikely]]
            return nullptr;

        return reinterpret_cast<processor *>(arch_self());
    }

    extern "C" std::uint8_t kernel_stack[];
    void init_bsp()
    {
        const auto smp = boot::requests::smp.response;

        processors = new processor[smp->cpu_count] { };
        bsp_aid = get_bsp_id(smp);

        for (std::size_t i = 0; i < smp->cpu_count; i++)
        {
            auto cpu = smp->cpus[i];
            const auto aid = get_arch_id(cpu);

            if (aid != bsp_aid)
                continue;

            log::info("cpu: {} {}: arch id: {}", "initialising BSP", i, aid);

            auto &proc = processors[i];
            proc.self = &proc;
            proc.idx = bsp_idx = i;
            proc.arch_id = aid;
            proc.stack_top = reinterpret_cast<std::uintptr_t>(kernel_stack) + boot::kernel_stack_size;

            cpu->extra_argument = reinterpret_cast<std::uintptr_t>(&proc);
            arch::core::bsp(cpu);
        }
    }

    extern "C" void cpu_entry(boot::limine_mp_info *);
    void init()
    {
        const auto smp = boot::requests::smp.response;
        log::info("cpu: number of available processors: {}", smp->cpu_count);

        cpu_count = smp->cpu_count;

        for (std::size_t i = 0; i < cpu_count; i++)
        {
            auto cpu = smp->cpus[i];
            const auto aid = get_arch_id(cpu);

            if (aid == bsp_aid)
                continue;

            log::info("cpu: {} {}: arch id: {}", "bringing up cpu", i, aid);

            auto &proc = processors[i];
            proc.self = &proc;
            proc.idx = i;
            proc.arch_id = aid;
            proc.stack_top = std::calloc<std::uintptr_t>(boot::kernel_stack_size, 1) + boot::kernel_stack_size;

            cpu->extra_argument = reinterpret_cast<std::uintptr_t>(&proc);
            cpu->goto_address = &cpu_entry;

            while (!processors[i].online)
                arch::pause();
        }
    }
} // namespace cpu