// Copyright (C) 2024-2026  ilobilo

export module drivers.output.serial;

import drivers.fs.dev.tty;
import std;

export namespace output::serial
{
    struct logger
    {
        void (*printc)(char);
        logger *next;

        constexpr logger(void (*func)(char))
            : printc { func }, next { nullptr } { }
    };
    void register_logger(logger &prn);

    void printc(char chr);

    struct driver
    {
        const std::string driver_name;
        const std::string name;
        const std::ssize_t name_base;

        const std::uint32_t major;
        const std::uint32_t minor_start;
        const std::uint32_t num_devices;

        driver(std::string_view driver_name, std::string_view name, std::ssize_t name_base,
            std::uint32_t major, std::uint32_t minor_start, std::uint32_t num_devices)
            : driver_name { driver_name }, name { name }, name_base { name_base },
              major { major }, minor_start { minor_start }, num_devices { num_devices } { }

        virtual ~driver() = default;

        virtual std::shared_ptr<fs::dev::tty::instance> create_instance(fs::dev::tty::driver *drv, std::uint32_t minor) = 0;
        virtual void destroy_instance(fs::dev::tty::driver *drv, std::shared_ptr<fs::dev::tty::instance> inst) = 0;
    };

    void register_driver(driver &drv);
} // export namespace output::serial