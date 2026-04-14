// Copyright (C) 2024-2025  ilobilo

export module x86_64.system.idt;

import x86_64.system.gdt;
import system.interrupts;
import std;

namespace cpu
{
    extern "C++" struct processor;
} // namespace cpu

export namespace x86_64::idt
{
    struct [[gnu::packed]] entry
    {
        std::uint16_t offset0;
        std::uint16_t selector;
        std::uint8_t ist;
        std::uint8_t typeattr;
        std::uint16_t offset1;
        std::uint32_t offset2;
        std::uint32_t zero;

        void set(void *isr, std::uint8_t _typeattr = 0x8E, std::uint8_t _ist = 0)
        {
            auto addr = reinterpret_cast<std::uintptr_t>(isr);

            offset0 = static_cast<std::uint16_t>(addr);
            offset1 = static_cast<std::uint16_t>(addr >> 16);
            offset2 = static_cast<std::uint32_t>(addr >> 32);

            selector = gdt::segment::code;
            ist = _ist;
            typeattr = _typeattr;

            zero = 0;
        }
    };

    struct [[gnu::packed]] reg
    {
        std::uint16_t limit;
        std::uint64_t base;

        void load() const
        {
            asm volatile ("cli; lidt %0" :: "memory"(*this));
        }
    };

    inline constexpr std::uint8_t irq(std::uint8_t num) { return num + 0x20; }

    constexpr std::size_t num_ints = 256;
    constexpr std::size_t num_preints = 20;
    constexpr std::uint8_t panic_int = irq(16);

    constexpr reg invalid { 0, 0 };

    std::array<entry, num_ints> &table();

    [[nodiscard]]
    auto handler_at(std::size_t cpuidx, std::uint8_t num) -> std::optional<std::reference_wrapper<interrupts::handler>>;

    void init();
    void init_on(cpu::processor *cpu);
} // namespace x86_64::idt