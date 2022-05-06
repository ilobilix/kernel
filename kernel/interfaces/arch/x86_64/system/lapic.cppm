// Copyright (C) 2024  ilobilo

export module x86_64.system.lapic;
import std;

export namespace x86_64::apic
{
    enum class timer_mode
    {
        periodic,
        oneshot
    };

    enum class dest
    {
        none = 0b00,
        self = 0b01,
        all = 0b10,
        all_noself = 0b11
    };

    class lapic
    {
        private:
        static inline std::uintptr_t _pmmio;
        static inline std::uintptr_t _mmio;

        bool _x2apic;

        static std::uint32_t to_x2apic(std::uint32_t reg)
        {
            return (reg >> 4) + 0x800;
        }

        std::uint32_t read(std::uint32_t reg) const;
        void write(std::uint32_t reg, std::uint32_t val) const;

        std::pair<bool, bool> supported() const;

        public:
        lapic();

        void eoi();
        void ipi(std::uint8_t id, dest dsh, std::uint8_t vector);
    };
} // export namespace x86_64::apic