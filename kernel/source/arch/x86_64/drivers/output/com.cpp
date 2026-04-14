// Copyright (C) 2024-2026  ilobilo

module x86_64.drivers.output.com;

import drivers.output.serial;
import drivers.output.terminal;
import drivers.fs.devtmpfs;
import drivers.fs.dev.tty;
import system.interrupts;
import system.cpu;
import system.vfs;
import magic_enum;
import arch;
import lib;
import fmt;
import std;

namespace x86_64::output::com
{
    namespace
    {
        enum COM
        {
            COM1 = 0x3F8,
            COM2 = 0x2F8,
            // COM3 = 0x3E8,
            // COM4 = 0x2E8,
            // COM5 = 0x5F8,
            // COM6 = 0x4F8,
            // COM7 = 0x5E8,
            // COM8 = 0x4E8,
        };

        constexpr std::size_t num_coms = 2;

        constexpr COM nth_com(std::size_t idx)
        {
            switch (idx)
            {
                case 1:
                    return COM1;
                case 2:
                    return COM2;
                // case 3:
                //     return COM3;
                // case 4:
                //     return COM4;
                // case 5:
                //     return COM5;
                // case 6:
                //     return COM6;
                // case 7:
                //     return COM7;
                // case 8:
                //     return COM8;
            }
            lib::panic("invalid com {}", idx);
            std::unreachable();
        }

        constinit std::array<bool, num_coms> usable { };
        std::array<std::function<void (char)>, num_coms> hooks;

        void irq_handler(cpu::registers *regs)
        {
            std::size_t idx = 0;
            if (regs->vector == 0x24)
                idx = 1;
            else if (regs->vector == 0x23)
                idx = 2;
            else
                lib::panic("invalid com irq vector {}", regs->vector);

            lib::bug_on(!usable[idx - 1]);

            const auto com = nth_com(idx);

            const auto iir = lib::io::in<8>(com + 2);
            const bool data = ((iir >> 1) & 0b11) == 0b10;
            if (!data)
                return;

            while (lib::io::in<8>(com + 5) & 1)
            {
                auto chr = lib::io::in<8>(com);
                if (hooks[idx - 1])
                    hooks[idx - 1](chr);
            }
        }

        void printc(char chr, COM com)
        {
            while (!(lib::io::in<8>(com + 5) & (1 << 5)))
                arch::pause();

            lib::io::out<8>(com, chr);
        }

        bool init_port(COM com)
        {
            lib::io::out<8>(com + 1, 0x00);
            lib::io::out<8>(com + 3, 0x80);
            lib::io::out<8>(com + 0, 0x01);
            lib::io::out<8>(com + 1, 0x00);
            lib::io::out<8>(com + 3, 0x03);
            lib::io::out<8>(com + 2, 0xC7);
            lib::io::out<8>(com + 4, 0x0B);

            lib::io::out<8>(com + 4, 0x1E);
            lib::io::out<8>(com + 0, 0xAE);
            if (lib::io::in<8>(com + 0) != 0xAE)
                return false;

            lib::io::out<8>(com + 4, 0x0F);
            lib::io::out<8>(com + 1, 0x01);
            return true;
        }
    } // namespace

    void printc(char chr)
    {
        printc(chr, COM1);
    }

    void init()
    {
        for (std::size_t i = 0; i < num_coms; i++)
            usable[i] = init_port(nth_com(i + 1));

        using namespace ::output::serial;
        static constinit logger log { printc };
        register_logger(log);
    }

    namespace tty = fs::dev::tty;
    using namespace ::output;

    struct serial_driver : serial::driver
    {
        struct serial_instance : tty::instance
        {
            std::size_t transmit(std::span<std::byte> buffer) override
            {
                const auto idx = minor - 64;
                if (!usable[idx])
                    return 0;

                const auto com = nth_com(idx + 1);
                for (const auto byte : buffer)
                {
                    const auto chr = static_cast<char>(byte);
                    printc(chr, com);

                    //! TODO: TEMPORARY - FOR TESTING
                    namespace term = ::output::term;
                    const auto ctx = term::main();
                    if (chr == '\n')
                        term::write(ctx, '\r');
                    term::write(ctx, chr);
                }
                return buffer.size_bytes();
            }

            std::size_t can_transmit() override
            {
                return std::numeric_limits<std::size_t>::max();
            }

            lib::expect<void> open(std::shared_ptr<vfs::file> self) override
            {
                lib::unused(self);
                const auto idx = minor - 64;
                if (usable[idx])
                {
                    hooks[idx] = [this](char chr) {
                        receive(std::span { reinterpret_cast<std::byte *>(&chr), 1 });
                    };
                    return { };
                }
                return std::unexpected { lib::err::invalid_device_or_address };
            }

            lib::expect<void> close() override
            {
                hooks[minor - 64] = nullptr;
                return { };
            };

            serial_instance(tty::driver *drv, std::uint32_t minor)
                : instance { drv, minor, std::make_unique<tty::default_ldisc>(this) } { }
        };

        // lib::map::flat_hash<std::uint32_t, std::shared_ptr<serial_instance>> instances;

        std::shared_ptr<fs::dev::tty::instance> create_instance(tty::driver *drv, std::uint32_t minor) override
        {
            // if (instances.contains(minor))
            //     return nullptr;
            // return instances.emplace(minor, std::make_shared<serial_instance>(drv, minor)).first->second;

            return std::make_shared<serial_instance>(drv, minor);
        }

        void destroy_instance(tty::driver *drv, std::shared_ptr<fs::dev::tty::instance> inst) override
        {
            // lib::unused(drv);
            // instances.erase(inst->minor);

            lib::unused(drv, inst);
        }

        serial_driver(std::string_view driver_name, std::string_view name, std::size_t name_base,
            std::uint32_t major, std::uint32_t minor_start, std::uint32_t num_devices, tty::flag flags)
            : driver { driver_name, name, name_base, major, minor_start, num_devices, flags } { }
    };

    serial_driver driver { "serial", "ttyS", 0, 4, 64, num_coms, tty::flag::none };

    lib::initgraph::task com_task
    {
        "output.arch.com.tty.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { fs::devtmpfs::mounted_stage() },
        [] {

            for (std::size_t i = 0; i < num_coms; i++)
            {
                if (usable[i])
                {
                    auto [handler, vector] = interrupts::allocate(cpu::bsp_idx(), 0x24 - i).value();
                    handler.set(irq_handler);
                    interrupts::unmask(vector);
                }
            }

            serial::register_driver(driver);
        }
    };
} // namespace x86_64::output::com