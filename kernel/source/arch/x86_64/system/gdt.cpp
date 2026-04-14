// Copyright (C) 2022-2024  ilobilo

module x86_64.system.gdt;

import system.cpu.self;
import system.memory;
import magic_enum;
import boot;
import lib;
import cppstd;

namespace x86_64::gdt
{
    namespace
    {
        [[gnu::naked]] void load(std::uintptr_t gdtr, std::uint8_t data, std::uint8_t code, std::uint16_t tss)
        {
            asm volatile (R"(
                lgdt [rdi]

                mov ss, si
                mov si, 0
                mov ds, si
                mov fs, si
                mov gs, si
                mov es, si

                push rdx
                lea rax, [rip + 1f]
                push rax
                retfq
                1:
                ltr cx
                ret
            )");
        }
    } // namespace

    cpu_local<entries> gdt_local;
    cpu_local<tss::ptr> tss_local;

    cpu_local_init(gdt_local);
    cpu_local_init(tss_local);

    namespace tss
    {
        ptr &self() { return tss_local.get(); }
    } // namespace tss

    void init_on(cpu::processor *cpu)
    {
        if (cpu->idx == cpu::bsp_idx())
            log::info("gdt: loading on bsp");

        auto allocate_stack = [] {
            const auto stack = vmm::alloc_vpages(vmm::space_type::other, boot::kstack_size / pmm::page_size);

            const auto flags = vmm::flag::rw;
            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);
            const auto npages = lib::div_roundup(npsize, pmm::page_size);

            for (std::size_t i = 0; i < boot::kstack_size; i += npsize)
            {
                const auto paddr = pmm::alloc<std::uintptr_t>(npages, true);
                if (auto ret = vmm::kernel_pagemap->map(stack + i, paddr, npsize, flags, psize); !ret)
                    lib::panic("could not map kernel stack: {}", magic_enum::enum_name(ret.error()));
            }

            return reinterpret_cast<std::uintptr_t>(stack) + boot::kstack_size;
        };
        tss_local->rsp[0] = allocate_stack(); // cpl3 to cpl0
        tss_local->ist[0] = allocate_stack(); // page fault
        tss_local->ist[1] = allocate_stack(); // scheduler

        tss_local->iopboffset = sizeof(tss::ptr);

        const auto base = reinterpret_cast<std::uintptr_t>(&tss_local.get());
        const std::uint16_t limit = sizeof(tss::ptr) - 1;

        gdt_local = entries {
            { 0x0000, 0x0000, 0x00, 0b00000000, 0x0, 0b0000, 0x00 }, // null
            { 0x0000, 0x0000, 0x00, 0b10011010, 0x0, 0b0010, 0x00 }, // kernel code
            { 0x0000, 0x0000, 0x00, 0b10010010, 0x0, 0b0010, 0x00 }, // kernel data
            { 0x0000, 0x0000, 0x00, 0b11111010, 0x0, 0b0100, 0x00 }, // user code 32 bit
            { 0x0000, 0x0000, 0x00, 0b11110010, 0x0, 0b0010, 0x00 }, // user data
            { 0x0000, 0x0000, 0x00, 0b11111010, 0x0, 0b0010, 0x00 }, // user code
            { // tss
                limit,
                static_cast<std::uint16_t>(base),
                static_cast<std::uint8_t>(base >> 16),
                0b10001001, 0x0, 0x0,
                static_cast<std::uint8_t>(base >> 24),
                static_cast<std::uint32_t>(base >> 32),
                0x00000000
            }
        };

        const ptr gdtr {
            sizeof(entries) - 1,
            reinterpret_cast<std::uintptr_t>(&gdt_local.get()),
        };

        load(reinterpret_cast<std::uintptr_t>(&gdtr), segment::data, segment::code, segment::tss);
    }
} // namespace x86_64::gdt