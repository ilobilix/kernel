// Copyright (C) 2024-2026  ilobilo

module drivers.output.serial;

import system.vfs;
import lib;

namespace output::serial
{
    namespace tty = fs::dev::tty;

    struct serial_driver : tty::driver
    {
        serial::driver &drv;

        std::shared_ptr<tty::instance> create_instance(std::uint32_t minor) override
        {
            return drv.create_instance(this, minor);
        }

        void destroy_instance(std::shared_ptr<tty::instance> inst) override
        {
            drv.destroy_instance(this, inst);
        }

        serial_driver(serial::driver &drv)
            : tty::driver { drv.driver_name,    drv.name,
                           drv.name_base,      drv.major,
                           drv.minor_start,    drv.num_devices,
                           drv.flags,          tty::type::serial,
                           tty::subtype::none, tty::ktermios::standard() },
              drv { drv }
        { }
    };

    void register_driver(serial::driver &drv) { tty::register_driver(new serial_driver { drv }); }
} // namespace output::serial
