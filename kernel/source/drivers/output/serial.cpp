// Copyright (C) 2024-2026  ilobilo

module drivers.output.serial;

import drivers.fs.dev.tty;
import system.vfs;
import lib;
import std;

namespace output::serial
{
    namespace
    {
        constinit logger *loggers = nullptr;
    } // namespace

    void register_logger(logger &log)
    {
        if (loggers == nullptr)
        {
            loggers = &log;
            log.next = nullptr;
            return;
        }
        else
        {
            log.next = loggers;
            loggers = &log;
        }
    }

    void printc(char chr)
    {
        auto current = loggers;
        while (current != nullptr)
        {
            current->printc(chr);
            current = current->next;
        }
    }

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

        serial_driver(serial::driver &drv) : tty::driver {
            drv.driver_name, drv.name, drv.name_base,
            drv.major, drv.minor_start, drv.num_devices,
            drv.flags, tty::type::serial, tty::subtype::none,
            tty::ktermios::standard()
        }, drv { drv } { }
    };

    void register_driver(serial::driver &drv)
    {
        tty::register_driver(new serial_driver { drv });
    }
} // namespace output::serial
