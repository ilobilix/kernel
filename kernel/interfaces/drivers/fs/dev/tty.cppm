// Copyright (C) 2024-2025  ilobilo

export module drivers.fs.dev.tty;

import system.vfs;
import lib;
import frigg;
import cppstd;

export namespace fs::dev::tty
{
    using cc_t = std::uint8_t;
    using speed_t = std::uint32_t;
    using tcflag_t = std::uint32_t;

    inline constexpr std::size_t NCCS = 19;
    struct termios
    {
        tcflag_t c_iflag;
        tcflag_t c_oflag;
        tcflag_t c_cflag;
        tcflag_t c_lflag;
        cc_t c_line;
        cc_t c_cc[NCCS];
    };

    struct winsize
    {
        std::uint16_t ws_row;
        std::uint16_t ws_col;
        std::uint16_t ws_xpixel;
        std::uint16_t ws_ypixel;
    };

    struct driver;
    struct instance
    {
        driver *drv;
        std::uint32_t minor;

        std::atomic<std::uint32_t> ref;

        lib::locker<termios, lib::rwmutex> termios;
        lib::locker<winsize, lib::rwmutex> winsize;

        struct ctrl_t
        {
            pid_t pgid;
            pid_t sid;
        };
        lib::locker<ctrl_t, lib::rwmutex> ctrl;

        std::weak_ptr<instance> link;

        frg::default_list_hook<instance> hook;

        instance(driver *drv, std::uint32_t minor);

        virtual bool open(std::shared_ptr<vfs::file> self) = 0;
        virtual bool close() = 0;

        virtual std::ssize_t read(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) = 0;
        virtual std::ssize_t write(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) = 0;
        virtual void flush_buffer() = 0;
        virtual int ioctl(unsigned long request, lib::uptr_or_addr argp) = 0;

        virtual ~instance() = default;
    };

    struct driver
    {
        const std::string name;
        std::uint32_t major, minor_start;

        const termios init_termios;

        std::weak_ptr<driver> other;
        lib::locker<
            lib::map::flat_hash<
                std::uint32_t,
                std::shared_ptr<instance>
            >, lib::rwmutex
        > instances;

        frg::default_list_hook<driver> hook;

        driver(std::string_view name, std::uint32_t major, std::uint32_t minor_start, const termios &init_termios)
            : name { name }, major { major }, minor_start { minor_start }, init_termios { init_termios } { }

        virtual std::shared_ptr<instance> create_instance(std::uint32_t minor) = 0;
        virtual void destroy_instance(std::shared_ptr<instance> inst) = 0;
        virtual int ioctl(std::shared_ptr<instance> inst, unsigned long request, lib::uptr_or_addr argp) = 0;

        virtual ~driver() = default;
    };

    lib::initgraph::stage *registered_stage();
} // export namespace fs::dev::tty