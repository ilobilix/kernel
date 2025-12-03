// Copyright (C) 2024-2025  ilobilo

module x86_64.system.pic;

import magic_enum;
import arch;
import lib;
import std;

namespace x86_64::pic
{
    namespace
    {
        enum class port : std::uint8_t
        {
            master = 0x20,
            master_command = master,
            master_data = (master + 1),

            slave = 0xA0,
            slave_command = slave,
            slave_data = (slave + 1)
        };

        enum class cmd : std::uint8_t
        {
            icw1_icw4 = 0x01,
            icw4_8086 = 0x01,
            icw1_init = 0x10,
            eoi = 0x20
        };
        using magic_enum::bitwise_operators::operator|;
    } // namespace

    void eoi(std::uint8_t vector)
    {
        lib::bug_on(vector < 0x20);
        if (vector >= 40)
            lib::io::out<8>(port::slave_command, cmd::eoi);
        lib::io::out<8>(port::master_command, cmd::eoi);
    }

    void mask(std::uint8_t vector)
    {
        lib::bug_on(vector < 0x20);
        lib::debug("pic: masking vector 0x{:X}", vector);

        auto port = port::master_data;
        auto irq = vector - 0x20;
        if (irq >= 8)
        {
            port = port::slave_data;
            irq -= 8;
        }
        lib::io::out<8>(port, lib::io::in<8>(port) | (1 << irq));
    }

    void unmask(std::uint8_t vector)
    {
        lib::bug_on(vector < 0x20);
        lib::debug("pic: unmasking vector 0x{:X}", vector);

        auto port = port::master_data;
        auto irq = vector - 0x20;
        if (irq >= 8)
        {
            port = port::slave_data;
            irq -= 8;
        }
        lib::io::out<8>(port, lib::io::in<8>(port) & ~(1 << irq));
    }

    void disable()
    {
        lib::debug("pic: masking all irqs");
        lib::io::out<8>(port::master_data, 0xFF);
        lib::io::out<8>(port::slave_data,  0xFF);
    }

    void init()
    {
        lib::info("pic: remapping");
        arch::int_switch(false);

        // auto i1 = lib::io::in<8>(port::master_data);
        // auto i2 = lib::io::in<8>(port::slave_data);

        lib::io::out<8>(port::master_command, cmd::icw1_init | cmd::icw1_icw4);
        lib::io::out<8>(port::slave_command,  cmd::icw1_init | cmd::icw1_icw4);
        lib::io::out<8>(port::master_data, 0x20);
        lib::io::out<8>(port::slave_data,  0x28);
        lib::io::out<8>(port::master_data, 0x04);
        lib::io::out<8>(port::slave_data,  0x02);
        lib::io::out<8>(port::master_data, cmd::icw4_8086);
        lib::io::out<8>(port::slave_data,  cmd::icw4_8086);

        // lib::io::out<8>(port::master_data, i1);
        // lib::io::out<8>(port::slave_data, i2);

        disable();
        unmask(0x20 + 2);
        arch::int_switch(true);
    }
} // namespace x86_64::pic