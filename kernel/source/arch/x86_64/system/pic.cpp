// Copyright (C) 2024  ilobilo

module x86_64.system.pic;

import magic_enum;
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
        if (vector >= 40)
            lib::io::out<8>(port::slave_command, cmd::eoi);
        lib::io::out<8>(port::master_command, cmd::eoi);
    }

    void mask(std::uint8_t vector)
    {
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
        log::debug("Masking all PIC IRQs");
        lib::io::out<8>(port::master_data, 0xFF);
        lib::io::out<8>(port::slave_data,  0xFF);
    }

    void init()
    {
        log::info("Initialising PIC");

        lib::io::out<8>(port::master_command, cmd::icw1_init | cmd::icw1_icw4);
        lib::io::out<8>(port::slave_command,  cmd::icw1_init | cmd::icw1_icw4);
        lib::io::out<8>(port::master_data, 0x20);
        lib::io::out<8>(port::slave_data,  0x28);
        lib::io::out<8>(port::master_data, 0x04);
        lib::io::out<8>(port::slave_data,  0x02);
        lib::io::out<8>(port::master_data, cmd::icw4_8086);
        lib::io::out<8>(port::slave_data,  cmd::icw4_8086);

        disable();
    }
} // namespace x86_64::pic