// Copyright (C) 2024-2025  ilobilo

export module drivers.fs.dev.tty;

import system.vfs;
import lib;
import std;

export namespace fs::dev::tty
{
    using cc_t = std::uint8_t;
    using speed_t = std::uint32_t;
    using tcflag_t = std::uint32_t;

    inline constexpr std::size_t knccs = 19;
    inline constexpr std::size_t unccs = 17;

    struct ktermios
    {
        enum /* iflag */ : tcflag_t
        {
            ignbrk  = 0000001, // ignore break condition
            brkint  = 0000002, // signal interrupt on break
            ignpar  = 0000004, // ignore characters with parity errors
            parmrk  = 0000010, // mark parity and framing errors
            inpck   = 0000020, // enable input parity check
            istrip  = 0000040, // strip 8th bit off characters
            inlcr   = 0000100, // map NL to CR on input
            igncr   = 0000200, // ignore CR
            icrnl   = 0000400, // map CR to NL on input
            iuclc   = 0001000, // map uppercase to lowercase on input (not in POSIX)
            ixon    = 0002000, // enable start/stop output control
            ixany   = 0004000, // enable any character to restart output
            ixoff   = 0010000, // enable start/stop input control
            imaxbel = 0020000, // ring bell when input queue is full (not in POSIX)
            iutf8   = 0040000, // input is UTF8 (not in POSIX)
        };

        enum /* oflag */ : tcflag_t
        {
            opost  = 0000001, // post-process output
            olcuc  = 0000002, // map lowercase to uppercase on output (not in POSIX)
            onlcr  = 0000004, // map NL to CR-NL on output
            ocrnl  = 0000010, // map CR to NL on output
            onocr  = 0000020, // no CR output at column 0
            onlret = 0000040, // NL performs CR function
            ofill  = 0000100, // use fill characters for delay
            ofdel  = 0000200, // fill is DEL
            ndly   = 0000400,
            crdly  = 0003000,
            tabdly = 0014000,
            bsdly  = 0020000,
            vtdly  = 0040000,
            ffdly  = 0100000
        };

        enum /* cflag */ : tcflag_t
        {
            csize  = 0000060,
            cs5    = 0000000,
            cs6    = 0000020,
            cs7    = 0000040,
            cs8    = 0000060,
            cstopb = 0000100,
            cread  = 0000200,
            parenb = 0000400,
            parodd = 0001000,
            hupcl  = 0002000,
            clocal = 0004000,
        };

        enum /* baud */ : tcflag_t
        {
            b0     = 0u, // hang up or ispeed == ospeed
            b50    = 50u,
            b75    = 75u,
            b110   = 110u,
            b134   = 134u, // really 134.5 baud by POSIX spec
            b150   = 150u,
            b200   = 200u,
            b300   = 300u,
            b600   = 600u,
            b1200  = 1200u,
            b1800  = 1800u,
            b2400  = 2400u,
            b4800  = 4800u,
            b9600  = 9600u,
            b19200 = 19200u,
            b38400 = 38400u,
        };

        enum /* lflag */ : tcflag_t
        {
            isig    = 0000001, // enable signals
            icanon  = 0000002, // canonical input (erase and kill processing)
            echo    = 0000010, // enable echo
            echoe   = 0000020, // echo erase character as error-correcting backspace
            echok   = 0000040, // echo KILL
            echonl  = 0000100, // echo NL
            noflsh  = 0000200, // disable flush after interrupt or quit
            tostop  = 0000400, // send SIGTTOU for background output
            echoctl = 0001000, // if ECHO is also set, terminal special characters
                               // other than TAB, NL, START, and STOP are echoed as
                               // ^X, where X is the character with ASCII code 0x40 greater than
                               // the special character (not in POSIX)
            echoprt = 0002000, // if ICANON and ECHO are also set, characters are
                               // printed as they are being erased (not in POSIX)
            echoke  = 0004000, // if ICANON is also set, KILL is echoed by erasing
                               // each character on the line, as specified by ECHOE
                               // and ECHOPRT (not in POSIX)
            iexten  = 0100000  // enable implementation-defined input processing
        };

        enum /* cc */ : cc_t
        {
            vintr = 0,
            vquit = 1,
            verase = 2,
            vkill = 3,
            veof = 4,
            vtime = 5,
            vmin = 6,
            vswtc = 7,
            vstart = 8,
            vstop = 9,
            vsusp = 10,
            veol = 11,
            vreprint = 12,
            vdiscard = 13,
            vwerase = 14,
            vlnext = 15,
            veol2 = 16
        };

        tcflag_t c_iflag;
        tcflag_t c_oflag;
        tcflag_t c_cflag;
        tcflag_t c_lflag;
        cc_t c_line;
        cc_t c_cc[knccs];
        speed_t c_ispeed;
        speed_t c_ospeed;

        static inline constexpr auto ctrl(cc_t c)
        {
            return c & 0x1F;
        }

        static constexpr ktermios standard()
        {
            ktermios t;
            {
                t.c_iflag = icrnl | ixon;
                t.c_oflag = opost | onlcr;
                t.c_cflag = std::to_underlying(b38400) | cs8 | cread | hupcl;
                t.c_lflag = isig | icanon | echo | echoe | echok | echoctl | echoke | iexten;
                t.c_line = 0;

                t.c_cc[vintr] = ctrl('C');
                t.c_cc[vquit] = ctrl('\\');
                t.c_cc[verase] = '\177';
                t.c_cc[vkill] = ctrl('U');
                t.c_cc[veof] = ctrl('D');
                t.c_cc[vstart] = ctrl('Q');
                t.c_cc[vstop] = ctrl('S');
                t.c_cc[vsusp] = ctrl('Z');
                t.c_cc[vreprint] = ctrl('R');
                t.c_cc[vdiscard] = ctrl('O');
                t.c_cc[vwerase] = ctrl('W');
                t.c_cc[vlnext] = ctrl('V');

                t.c_cc[vmin] = 1;
                t.c_cc[vtime] = 0;

                t.c_ispeed = std::to_underlying(b38400);
                t.c_ospeed = std::to_underlying(b38400);
            }
            return t;
        }
    };

    struct utermios
    {
        tcflag_t c_iflag;
        tcflag_t c_oflag;
        tcflag_t c_cflag;
        tcflag_t c_lflag;
        cc_t c_line;
        cc_t c_cc[unccs];
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

        line_discipline(instance *inst) : inst { inst } { open(); }

        virtual ~line_discipline() { close(); }

        virtual std::ssize_t read(lib::maybe_uspan<std::byte> buffer) = 0;
        virtual std::ssize_t write(lib::maybe_uspan<std::byte> buffer) = 0;

        virtual void receive(std::span<std::byte> buffer) = 0;

        virtual void open() { }
        virtual void close() { }
    };

    struct default_ldisc : line_discipline
    {
        lib::rbspmco<char, 4096> raw_buffer;
        lib::rbspmco<std::string, 4096> cooked_buffer;

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

        lib::locker<ktermios, lib::rwmutex> termios;
        lib::locker<winsize, lib::rwmutex> winsize;

        struct ctrl_t
        {
            pid_t pgid;
            pid_t sid;
        };
        lib::locker<ctrl_t, lib::mutex> ctrl;

        std::weak_ptr<instance> link;

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

        const ktermios init_termios;

        std::weak_ptr<driver> other;
        lib::locker<
            lib::map::flat_hash<
                std::uint32_t,
                std::shared_ptr<instance>
            >, lib::mutex
        > instances;

        lib::intrusive_list_hook<driver> hook;

        driver(std::string_view name, std::uint32_t major, std::uint32_t minor_start, const ktermios &init_termios)
            : name { name }, major { major }, minor_start { minor_start }, init_termios { init_termios } { }

        virtual ~driver() = default;

        virtual std::shared_ptr<instance> create_instance(std::uint32_t minor) = 0;
        virtual void destroy_instance(std::shared_ptr<instance> inst) = 0;
        virtual int ioctl(std::shared_ptr<instance> inst, unsigned long request, lib::uptr_or_addr argp) = 0;
    };

    lib::initgraph::stage *registered_stage();
} // export namespace fs::dev::tty