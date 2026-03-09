// Copyright (C) 2024-2026  ilobilo

module x86_64.drivers.output.uart8250;

import drivers.output.serial;
import drivers.output.terminal;
import drivers.fs.devtmpfs;
import drivers.fs.dev.tty;
import system.interrupts;
import system.cpu;
import system.vfs;
import arch;
import lib;
import std;

namespace x86_64::output::uart8250
{
    namespace
    {
        constexpr std::size_t num_ports = 4;
        constexpr std::array<std::uint16_t, num_ports> ports
        {
            // irq: 4, 3, 4, 3
            0x3F8, 0x2F8, 0x3E8, 0x2E8
        };

        constinit std::array<bool, num_ports> usable { };
        std::array<std::function<void (char)>, num_ports> hooks;

        void irq_handler(cpu::registers *regs)
        {
            const auto read = [](std::size_t idx)
            {
                const auto port = ports[idx];

                const auto iir = lib::io::in<8>(port + 2);
                const bool data = ((iir >> 1) & 0b11) == 0b10;
                if (!data)
                    return;

                while (lib::io::in<8>(port + 5) & 1)
                {
                    auto chr = lib::io::in<8>(port);
                    if (hooks[idx])
                        hooks[idx](chr);
                }
                return;
            };

            if (regs->vector == 0x24)
            {
                read(0);
                read(2);
            }
            else if (regs->vector == 0x23)
            {
                read(1);
                read(3);
            }
        }

        void printc(char chr, std::uint16_t port)
        {
            while (!(lib::io::in<8>(port + 5) & (1 << 5)))
                arch::pause();

            lib::io::out<8>(port, chr);
        }

        bool init_port(std::uint16_t port)
        {
            lib::io::out<8>(port + 1, 0x00); // disable interrupts
            lib::io::out<8>(port + 3, 0x80); // set DLAB

            lib::io::out<8>(port + 0, 0x03); // DLL - lo 38400 bps
            lib::io::out<8>(port + 1, 0x00); // DLM - hi

            lib::io::out<8>(port + 3, 0x03); // 8 data bots, one stop bit, no parity
            lib::io::out<8>(port + 2, 0xC7); // enable FIFO, clear rx/tx, 14-byte threshold
            lib::io::out<8>(port + 4, 0x0B); // ready

            // test by writing to scratch register
            lib::io::out<8>(port + 7, 0xAE);
            if (lib::io::in<8>(port + 7) != 0xAE)
                return false;

            lib::io::out<8>(port + 4, 0x0F);
            return true;
        }

        void irq_swtich(std::uint16_t port, bool on)
        {
            lib::io::out<8>(port + 1, on);
        }
    } // namespace

    void init()
    {
        for (std::size_t i = 0; i < num_ports; i++)
            usable[i] = init_port(ports[i]);

        using namespace ::output::serial;
        static constinit logger log { [](char chr) { printc(chr, ports[0]); } };
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

                const auto com = ports[idx];
                for (const auto byte : buffer)
                {
                    const auto chr = static_cast<char>(byte);
                    printc(chr, com);

                    //! TODO: TEMPORARY - FOR TESTING
                    namespace term = ::output::term;
                    const auto ctx = term::main();
                    term::write(ctx, chr);
                }
                return buffer.size_bytes();
            }

            std::size_t can_transmit() override
            {
                return std::numeric_limits<std::size_t>::max();
            }

            lib::expect<void> open(std::shared_ptr<vfs::file> file) override
            {
                lib::unused(file);

                const auto idx = minor - 64;
                if (!usable[idx])
                    return std::unexpected { lib::err::invalid_device_or_address };

                hooks[idx] = [this](char chr) {
                    receive(std::span { reinterpret_cast<std::byte *>(&chr), 1 });
                };
                return { };
            }

            lib::expect<void> close() override
            {
                hooks[minor - 64] = nullptr;
                return { };
            };

            serial_instance(tty::driver *drv, std::uint32_t minor)
                : instance { drv, minor, std::make_unique<tty::default_ldisc>(this) } { }
        };

        std::shared_ptr<fs::dev::tty::instance> create_instance(tty::driver *drv, std::uint32_t minor) override
        {
            return std::make_shared<serial_instance>(drv, minor);
        }

        void destroy_instance(tty::driver *drv, std::shared_ptr<fs::dev::tty::instance> inst) override
        {
            lib::unused(drv, inst);
        }

        serial_driver(std::string_view driver_name, std::string_view name, std::size_t name_base,
            std::uint32_t major, std::uint32_t minor_start, std::uint32_t num_devices, tty::flag flags)
            : driver { driver_name, name, name_base, major, minor_start, num_devices, flags } { }
    };

    serial_driver driver { "serial", "ttyS", 0, 4, 64, num_ports, tty::flag::none };

    lib::initgraph::task com_task
    {
        "output.arch.uart8250.tty.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { fs::devtmpfs::mounted_stage() },
        [] {

            for (std::size_t i = 0; i < num_ports; i++)
            {
                if (usable[i])
                {
                    lib::info("uart8250: port {} is usable", i);
                    if (i < 2 || !usable[i - 2])
                    {
                        auto ret = interrupts::allocate(cpu::bsp_idx(), 0x24 - (i % 2));
                        lib::bug_on(!ret.has_value());
                        auto [handler, vector] = *ret;
                        handler.set(irq_handler);
                        interrupts::unmask(vector);
                    }
                    irq_swtich(ports[i], true);
                }
            }

            serial::register_driver(driver);
        }
    };
} // namespace x86_64::output::uart8250
