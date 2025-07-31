// Copyright (C) 2024-2025  ilobilo

export module x86_64.system.lapic;
import cppstd;

export namespace x86_64::apic
{
    namespace reg
    {
        constexpr std::uintptr_t apic_base = 0x1B;
        constexpr std::uintptr_t id = 0x20;
        constexpr std::uintptr_t tpr = 0x80;
        constexpr std::uintptr_t siv = 0xF0;
        constexpr std::uintptr_t icrl = 0x300;
        constexpr std::uintptr_t icrh = 0x310;
        constexpr std::uintptr_t lvt = 0x320;
        constexpr std::uintptr_t tdc = 0x3E0;
        constexpr std::uintptr_t tic = 0x380;
        constexpr std::uintptr_t tcc = 0x390;

        constexpr std::uintptr_t deadline = 0x6E0;
    } // namespace reg

    enum class dest
    {
        id = 0b00,
        self = 0b01,
        all = 0b10,
        all_noself = 0b11
    };

    std::pair<bool, bool> supported();

    std::uint32_t read(std::uint32_t reg);
    void write(std::uint32_t reg, std::uint64_t val);

    void calibrate_timer();

    void eoi();
    void ipi(std::uint32_t id, dest dsh, std::uint64_t args);
    void arm(std::size_t ns, std::uint8_t vector);

    void init_cpu();
} // export namespace x86_64::apic