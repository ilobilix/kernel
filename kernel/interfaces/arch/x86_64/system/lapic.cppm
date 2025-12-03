// Copyright (C) 2024-2025  ilobilo

export module x86_64.system.lapic;
import std;

export namespace x86_64::apic
{
    namespace reg
    {
        constexpr std::uintptr_t apic_base = 0x1B;
        constexpr std::uintptr_t id = 0x20;
        constexpr std::uintptr_t tpr = 0x80;
        constexpr std::uintptr_t siv = 0xF0;
        constexpr std::uintptr_t icr = 0x300;
        constexpr std::uintptr_t icrh = 0x310;
        constexpr std::uintptr_t lvt = 0x320;
        constexpr std::uintptr_t tdc = 0x3E0;
        constexpr std::uintptr_t tic = 0x380;
        constexpr std::uintptr_t tcc = 0x390;

        constexpr std::uintptr_t deadline = 0x6E0;
    } // namespace reg

    enum class shorthand
    {
        id = 0b00,
        self = 0b01,
        all = 0b10,
        all_noself = 0b11
    };

    enum class delivery
    {
        fixed = 0b000,
        lprio = 0b001,
        smi = 0b010,
        nmi = 0b100,
        init = 0b101,
        startup = 0b110
    };

    enum class destination
    {
        physical = 0,
        logical = 1
    };

    std::pair<bool, bool> supported();
    bool is_initialised();

    std::uint32_t read(std::uint32_t reg);
    void write(std::uint32_t reg, std::uint64_t val);

    void calibrate_timer();

    void eoi();
    void ipi(shorthand dest, delivery del, std::uint8_t vec);
    void ipi(std::uint32_t id, destination dest, delivery del, std::uint8_t vec);

    void arm(std::size_t ns, std::uint8_t vector);

    void init_cpu();
} // export namespace x86_64::apic