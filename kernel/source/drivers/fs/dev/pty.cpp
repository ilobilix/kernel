// Copyright (C) 2024-2026  ilobilo

module drivers.fs.dev.pty;

import drivers.fs.devtmpfs;
import drivers.fs.dev.tty;
import system.vfs;
import system.vfs.dev;
import lib;
import std;

import magic_enum;
import fmt;

namespace fs::dev::pty
{
    namespace
    {
        constexpr std::size_t master_major = 128;
        constexpr std::size_t master_count = 8;
        constexpr std::size_t slave_major = master_major + master_count;

        constexpr tty::ktermios def_termios = [] {
            using enum tty::ktermios::cflag;
            using enum tty::ktermios::baud;
            auto ret = tty::ktermios::standard();
            ret.c_iflag = 0;
            ret.c_oflag = 0;
            ret.c_cflag = static_cast<tty::tcflag_t>(b38400) | cs8 | cread;
            ret.c_lflag = 0;
            ret.c_ispeed = 38400;
            ret.c_ospeed = 38400;
            return ret;
        } ();
    } // namespace

    struct ptm_driver : tty::driver
    {
        struct ptm_instance : tty::instance
        {
            std::size_t transmit(std::span<std::byte> buffer) override
            {
                return buffer.size_bytes();
            }

            std::size_t can_transmit() override
            {
                return std::numeric_limits<std::size_t>::max();
            }

            lib::expect<void> open(std::shared_ptr<vfs::file> self) override
            {
                lib::unused(self);
                return std::unexpected { lib::err::invalid_device_or_address };
            }

            lib::expect<void> close() override
            {
                return { };
            };

            ptm_instance(driver *drv, std::uint32_t minor)
                : instance { drv, minor, std::make_unique<tty::default_ldisc>(this) } { }
        };

        std::shared_ptr<tty::instance> create_instance(std::uint32_t minor) override
        {
            return std::make_shared<ptm_instance>(this, minor);
        }

        void destroy_instance(std::shared_ptr<tty::instance> inst) override
        {
            lib::unused(inst);
        }

        ptm_driver() : driver {
            "pty_master", "ptm", 0, master_major, 0, 0,
            tty::flag::dynamic, tty::type::pty, tty::subtype::pty_master,
            def_termios
        } { }
    };

    struct pts_driver : tty::driver
    {
        struct pts_instance : tty::instance
        {
            std::size_t transmit(std::span<std::byte> buffer) override
            {
                return buffer.size_bytes();
            }

            std::size_t can_transmit() override
            {
                return std::numeric_limits<std::size_t>::max();
            }

            lib::expect<void> open(std::shared_ptr<vfs::file> self) override
            {
                lib::unused(self);
                return std::unexpected { lib::err::invalid_device_or_address };
            }

            lib::expect<void> close() override
            {
                return { };
            };

            pts_instance(driver *drv, std::uint32_t minor)
                : instance { drv, minor, std::make_unique<tty::default_ldisc>(this) } { }
        };

        std::shared_ptr<tty::instance> create_instance(std::uint32_t minor) override
        {
            return std::make_shared<pts_instance>(this, minor);
        }

        void destroy_instance(std::shared_ptr<tty::instance> inst) override
        {
            lib::unused(inst);
        }

        pts_driver() : driver {
            "pty_slave", "pts", 0, slave_major, 0, 0,
            tty::flag::dynamic, tty::type::pty, tty::subtype::pty_slave,
            def_termios
        } { }
    };
} // namespace fs::dev::pty