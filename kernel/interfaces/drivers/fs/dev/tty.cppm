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

    struct instance;
    struct line_discipline
    {
        instance *inst;

        line_discipline(instance *inst) : inst { inst } { }

        virtual ~line_discipline() = default;

        virtual std::ssize_t read(lib::maybe_uspan<std::byte> buffer) = 0;
        virtual std::ssize_t write(lib::maybe_uspan<std::byte> buffer) = 0;

        virtual void receive(std::span<std::byte> buffer) = 0;

        virtual void open() { }
        virtual void close() { }
    };

    struct default_ldisc : line_discipline
    {
        default_ldisc(instance *inst) : line_discipline { inst } { }

        std::ssize_t read(lib::maybe_uspan<std::byte> buffer) override;
        std::ssize_t write(lib::maybe_uspan<std::byte> buffer) override;

        void receive(std::span<std::byte> buffer) override;
    };

    struct driver;
    struct instance
    {
        driver *drv;
        std::uint32_t minor;

        std::atomic<std::uint32_t> ref;

        lib::locker<std::unique_ptr<line_discipline>, lib::rwmutex> ldisc;

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

        instance(driver *drv, std::uint32_t minor, std::unique_ptr<line_discipline> ldisc);

        virtual ~instance() = default;

        virtual std::ssize_t read(lib::maybe_uspan<std::byte> buffer)
        {
            const auto rlocked = ldisc.read_lock();
            if (rlocked.value())
                return rlocked.value()->read(buffer);
            return -1;
        }

        virtual std::ssize_t write(lib::maybe_uspan<std::byte> buffer)
        {
            const auto rlocked = ldisc.read_lock();
            if (rlocked.value())
                return rlocked.value()->write(buffer);
            return -1;
        }

        // send to hardware
        virtual std::ssize_t transmit(lib::maybe_uspan<std::byte> buffer) = 0;
        // called by hardware
        void receive(std::span<std::byte> buffer)
        {
            const auto rlocked = ldisc.read_lock();
            if (rlocked.value())
                rlocked.value()->receive(buffer);
        }

        virtual bool open(std::shared_ptr<vfs::file> self) = 0;
        virtual bool close() = 0;

        virtual void flush_buffer() = 0;
        virtual int ioctl(unsigned long request, lib::uptr_or_addr argp) = 0;
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
            >, lib::mutex
        > instances;

        frg::default_list_hook<driver> hook;

        driver(std::string_view name, std::uint32_t major, std::uint32_t minor_start, const termios &init_termios)
            : name { name }, major { major }, minor_start { minor_start }, init_termios { init_termios } { }

        virtual ~driver() = default;

        virtual std::shared_ptr<instance> create_instance(std::uint32_t minor) = 0;
        virtual void destroy_instance(std::shared_ptr<instance> inst) = 0;
        virtual int ioctl(std::shared_ptr<instance> inst, unsigned long request, lib::uptr_or_addr argp) = 0;
    };

    lib::initgraph::stage *registered_stage();
} // export namespace fs::dev::tty