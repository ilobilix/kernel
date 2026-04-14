// Copyright (C) 2024-2025  ilobilo

module x86_64.drivers.timers.pit;

import system.interrupts;
import system.time;
import system.cpu;
import magic_enum;
import arch;
import lib;
import cppstd;

namespace x86_64::timers::pit
{
    namespace
    {
        enum class port
        {
            channel0 = 0x40,
            channel1 = 0x41,
            channel2 = 0x42,
            command = 0x43
        };

        enum class cmd
        {
            channel0 = (0b00 << 6),
            channel1 = (0b01 << 6),
            channel2 = (0b10 << 6),
            readback = (0b11 << 6),

            latch = (0b00 << 4),
            accesslo = (0b01 << 4),
            accesshi = (0b10 << 4),
            accesslh = (0b11 << 4),

            mode0 = (0b000 << 1), // terminal count
            mode1 = (0b001 << 1), // hard oneshot
            mode2 = (0b010 << 1), // rate
            mode3 = (0b011 << 1), // square wave
            mode4 = (0b100 << 1), // soft strobe
            mode5 = (0b101 << 1), // hard strobe
        };
        using magic_enum::bitwise_operators::operator|;

        std::size_t tick = 0;
        bool initialised = false;
    } // namespace

    bool is_initialised() { return initialised; }

    std::uint64_t time_ns()
    {
        return ((tick * 1'000) / frequency) * 1'000'000ul;
    }

    time::clock clock { "pit", 0, time_ns };
    void init()
    {
        const std::uint16_t divisor = 1193180 / frequency;
        const std::uint8_t low = divisor & 0xFF;
        const std::uint8_t high = (divisor >> 8) & 0xFF;

        log::info("pit: setting up with frequency {} hz", frequency);

        lib::io::out<8>(port::command, cmd::mode2 | cmd::accesslh);
        lib::io::out<8>(port::channel0, low);
        lib::io::out<8>(port::channel0, high);

        auto [handler, vector] = interrupts::allocate(cpu::bsp_idx(), 0x20).value();
        handler.set([](auto) { tick++; });
        interrupts::unmask(vector);

        time::register_clock(clock);
        initialised = true;
    }
} // namespace x86_64::timers::pit