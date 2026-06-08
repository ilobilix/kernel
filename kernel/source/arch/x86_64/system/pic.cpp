// Copyright (C) 2024-2026  ilobilo

module x86_64.system.pic;

import arch;
import lib;

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

        enum cmd : std::uint8_t
        {
            icw1_icw4 = 0x01,
            icw4_8086 = 0x01,
            icw1_init = 0x10,
            eoi = 0x20
        };
    } // namespace

    void init()
    {
        // lib::info("pic: remapping");
        arch::int_switch(false);

        lib::io::out<8>(port::master_command, cmd::icw1_init | cmd::icw1_icw4);
        lib::io::out<8>(port::slave_command, cmd::icw1_init | cmd::icw1_icw4);
        lib::io::out<8>(port::master_data, 0x20);
        lib::io::out<8>(port::slave_data, 0x28);
        lib::io::out<8>(port::master_data, 0x04);
        lib::io::out<8>(port::slave_data, 0x02);
        lib::io::out<8>(port::master_data, cmd::icw4_8086);
        lib::io::out<8>(port::slave_data, cmd::icw4_8086);

        lib::debug("pic: masking all irqs");
        lib::io::out<8>(port::master_data, 0xFF);
        lib::io::out<8>(port::slave_data, 0xFF);

        arch::int_switch(true);
    }
} // namespace x86_64::pic
