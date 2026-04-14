// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.dev.tty;

import system.sched.thread_base;
import system.vfs;
import lib;
import std;

export namespace fs::dev::tty
{
    using cc_t = std::uint8_t;
    using speed_t = std::uint32_t;
    using tcflag_t = std::uint32_t;

    inline constexpr std::size_t nccs = 19;

    struct ktermios
    {
        enum iflag : tcflag_t
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

        enum oflag : tcflag_t
        {
            opost  = 0x00001, // post-process output
            ocrnl  = 0x00008, // map CR to NL on output
            onocr  = 0x00010, // don't output CR at column 0
            onlret = 0x00020, // don't output CR
            ofill  = 0x00040, // send fill characters for a delay
            ofdel  = 0x00080, // fill character is ASCII DEL
            olcuc  = 0x00002, // map lowercase to uppercase on output (not in POSIX)
            onlcr  = 0x00004, // map NL to CR-NL on output
            nldly  = 0x00100, // newline delay mask
            nl0    = 0x00000, // newline delay 0
            nl1    = 0x00100, // newline delay 1
            crdly  = 0x00600, // carriage return delay mask
            cr0    = 0x00000, // carriage return delay 0
            cr1    = 0x00200, // carriage return delay 1
            cr2    = 0x00400, // carriage return delay 2
            cr3    = 0x00600, // carriage return delay 3
            tabdly = 0x01800, // horizontal tab delay mask
            tab0   = 0x00000, // horizontal tab delay 0
            tab1   = 0x00800, // horizontal tab delay 1
            tab2   = 0x01000, // horizontal tab delay 2
            tab3   = 0x01800, // expand tabs to spaces
            xtabs  = 0x01800, // expand tabs to spaces (synonym for tab3)
            bsdly  = 0x02000, // backspace delay mask
            bs0    = 0x00000, // backspace delay 0
            bs1    = 0x02000, // backspace delay 1
            vtdly  = 0x04000, // vertical tab delay mask
            vt0    = 0x00000, // vertical tab delay 0
            vt1    = 0x04000, // vertical tab delay 1
            ffdly  = 0x08000, // form feed delay mask
            ff0    = 0x00000, // form feed delay 0
            ff1    = 0x08000  // form feed delay 1
        };

        enum cflag : tcflag_t
        {
            cbaud   = 0x0000100F, // baud speed mask
            csize   = 0x00000030, // character size mask
            cs5     = 0x00000000, // 5 bits per byte
            cs6     = 0x00000010, // 6 bits per byte
            cs7     = 0x00000020, // 7 bits per byte
            cs8     = 0x00000030, // 8 bits per byte
            cstopb  = 0x00000040, // set two stop bits, rather than one
            cread   = 0x00000080, // enable receiver
            parenb  = 0x00000100, // enable parity generation and checking
            parodd  = 0x00000200, // parity for input and output is odd
            hupcl   = 0x00000400, // lower modem control lines after last process closes
            clocal  = 0x00000800, // ignore modem control lines
            cbaudex = 0x00001000, // extended baud rate mask
            bother  = 0x00001000, // non-standard integer baud rates
            cibaud  = 0x100f0000  // input baud rate mask
        };

        enum baud : speed_t
        {
            b0       = 0x00000000,
            b50      = 0x00000001,
            b75      = 0x00000002,
            b110     = 0x00000003,
            b134     = 0x00000004,
            b150     = 0x00000005,
            b200     = 0x00000006,
            b300     = 0x00000007,
            b600     = 0x00000008,
            b1200    = 0x00000009,
            b1800    = 0x0000000A,
            b2400    = 0x0000000B,
            b4800    = 0x0000000C,
            b9600    = 0x0000000D,
            b19200   = 0x0000000E,
            b38400   = 0x0000000F,
            b57600   = 0x00001001,
            b115200  = 0x00001002,
            b230400  = 0x00001003,
            b460800  = 0x00001004,
            b500000  = 0x00001005,
            b576000  = 0x00001006,
            b921600  = 0x00001007,
            b1000000 = 0x00001008,
            b1152000 = 0x00001009,
            b1500000 = 0x0000100A,
            b2000000 = 0x0000100B,
            b2500000 = 0x0000100C,
            b3000000 = 0x0000100D,
            b3500000 = 0x0000100E,
            b4000000 = 0x0000100F
        };

        enum lflag : tcflag_t
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

        enum cc : cc_t
        {
            vintr    = 0,  // interrupt character
            vquit    = 1,  // quit character
            verase   = 2,  // erase character
            vkill    = 3,  // kill-line character
            veof     = 4,  // end-of-file character
            vtime    = 5,  // time-out value (non-canonical)
            vmin     = 6,  // minimum number of characters (non-canonical)
            vswtc    = 7,  // switch character
            vstart   = 8,  // start character (XON)
            vstop    = 9,  // stop character (XOFF)
            vsusp    = 10, // suspend character
            veol     = 11, // end-of-line character
            vreprint = 12, // reprint-line character
            vdiscard = 13, // discard character
            vwerase  = 14, // word-erase character
            vlnext   = 15, // literal-next character
            veol2    = 16  // alternative end-of-line character
        };

        tcflag_t c_iflag;
        tcflag_t c_oflag;
        tcflag_t c_cflag;
        tcflag_t c_lflag;
        cc_t c_line;
        cc_t c_cc[nccs];
        speed_t c_ispeed;
        speed_t c_ospeed;

        static inline constexpr auto ctrl(cc_t c)
        {
            return c & 0x1F;
        }

        speed_t get_ispeed() const
        {
            if ((c_cflag & cbaud) == bother)
                return c_ispeed;
            const auto ispeed = (c_cflag & cibaud) >> 16;
            return ispeed == 0 ? (c_cflag & cbaud) : ispeed;
        }

        speed_t get_ospeed() const
        {
            if ((c_cflag & cbaud) == bother)
                return c_ospeed;
            return c_cflag & cbaud;
        }

        void set_ispeed(speed_t ispeed)
        {
            c_ispeed = ispeed;
            if ((c_cflag & cbaud) != bother)
            {
                c_cflag &= ~cibaud;
                c_cflag |= (ispeed << 16) & cibaud;
            }
        }

        void set_ospeed(speed_t ospeed)
        {
            c_ospeed = ospeed;
            if ((c_cflag & cbaud) != bother)
            {
                c_cflag &= ~cbaud;
                c_cflag |= ospeed & cbaud;
            }
        }

        static constexpr ktermios standard()
        {
            ktermios t { };
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
        cc_t c_cc[nccs];
    };

    struct winsize
    {
        std::uint16_t ws_row;
        std::uint16_t ws_col;
        std::uint16_t ws_xpixel;
        std::uint16_t ws_ypixel;

        static constexpr winsize standard()
        {
            return {
                .ws_row = 24,
                .ws_col = 80,
                .ws_xpixel = 0,
                .ws_ypixel = 0
            };
        }
    };

    enum ioctl
    {
        tiocgpgrp = 0x540F,
        tiocspgrp = 0x5410,
        tiocgwinsz = 0x5413,
        tiocswinsz = 0x5414,
        tcgets2 = 0x802C542A,
        tcsetsw2 = 0x402C542C
    };

    struct instance;
    struct line_discipline
    {
        instance *inst;

        line_discipline(instance *inst) : inst { inst } { }
        virtual ~line_discipline() = default;

        virtual void open() = 0;

        virtual lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer) = 0;
        virtual lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer) = 0;

        virtual lib::expect<int> ioctl(std::uint64_t request, lib::uptr_or_addr argp) = 0;

        virtual lib::expect<std::uint16_t> poll(vfs::poll_table *pt) = 0;

        virtual void receive(std::span<std::byte> buffer) = 0;

        virtual void hangup() = 0;
    };

    struct default_ldisc : line_discipline
    {
        static constexpr std::size_t buffer_size = 4096;

        struct in_buffer_t
        {
            static constexpr std::size_t cap = buffer_size;

            std::array<char, cap> data;

            std::size_t read_tail = 0;
            std::size_t read_head = 0;
            std::size_t cooked_head = 0;

            std::size_t space() const
            {
                return cap - (read_head - read_tail);
            }

            bool full() const
            {
                return space() == 0;
            }

            bool empty() const
            {
                return space() == cap;
            }

            bool push(char chr)
            {
                if (full())
                    return false;
                data[read_head % cap] = chr;
                read_head++;
                return true;
            }

            std::optional<char> pop_last()
            {
                if (read_head > cooked_head)
                {
                    read_head--;
                    return data[read_head % cap];
                }
                return std::nullopt;
            }

            bool erase()
            {
                if (read_head > cooked_head)
                {
                    read_head--;
                    return true;
                }
                return false;
            }

            void commit()
            {
                cooked_head = read_head;
            }

            char peek() const
            {
                if (read_head > cooked_head)
                    return data[(read_head - 1) % cap];
                return '\0';
            }
        };

        lib::rbspscd<std::byte, buffer_size> raw_buffer;
        lib::semaphore raw_sem;

        lib::locker<in_buffer_t, lib::mutex> in_buffer;
        lib::semaphore in_sem;
        lib::wait_queue in_poll_wq;

        lib::rbmpscd<char, buffer_size> out_buffer;
        lib::semaphore out_sem;
        lib::wait_queue out_poll_wq;

        std::atomic_bool stopped;

        sched::thread_base_t *worker_thread;
        std::atomic_bool should_work;
        lib::semaphore hung_sem;

        default_ldisc(instance *inst);
        ~default_ldisc();

        void open() override;
        void hangup() override;

        bool output_append(const ktermios &termios, char chr);
        void output_flush();

        [[noreturn]]
        static void worker(default_ldisc *self);

        void receive(std::span<std::byte> buffer) override;

        lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer) override;
        lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer) override;

        lib::expect<int> ioctl(std::uint64_t request, lib::uptr_or_addr argp) override;

        lib::expect<std::uint16_t> poll(vfs::poll_table *pt) override;
    };

    struct driver;
    struct instance
    {
        driver *drv;
        std::uint32_t minor;
        std::atomic<std::uint32_t> ref;

        std::atomic_bool hung_up;

        lib::locker<std::unique_ptr<line_discipline>, lib::rwmutex> ldisc;

        lib::locker<ktermios, lib::rwmutex> termios;
        lib::locker<winsize, lib::rwmutex> winsize;

        struct ctrl_t
        {
            // ugly
            void *group;
            void *session;
        };
        lib::locker<ctrl_t, lib::mutex> ctrl;

        std::weak_ptr<instance> link;

        instance(driver *drv, std::uint32_t minor, std::unique_ptr<line_discipline> ldisc);
        virtual ~instance() = default;

        virtual lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer)
        {
            const auto rlocked = ldisc.read_lock();
            if (rlocked.value())
                return rlocked.value()->read(std::move(file), buffer);
            return std::unexpected { lib::err::io_error };
        }

        virtual lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer)
        {
            const auto rlocked = ldisc.read_lock();
            if (rlocked.value())
                return rlocked.value()->write(std::move(file), buffer);
            return std::unexpected { lib::err::io_error };
        }

        virtual std::size_t transmit(std::span<std::byte> buffer) = 0;
        virtual std::size_t can_transmit() = 0;

        virtual lib::expect<void> open(std::shared_ptr<vfs::file> file) = 0;
        virtual lib::expect<void> close() = 0;

        virtual lib::expect<int> ioctl(std::uint64_t request, lib::uptr_or_addr argp);

        virtual lib::expect<std::uint16_t> poll(vfs::poll_table *pt);

        virtual void set_termios(ktermios &current, const ktermios &old)
        {
            lib::unused(current, old);
        };

        // called by hardware
        bool receive(std::span<std::byte> buffer)
        {
            const auto rlocked = ldisc.read_lock();
            if (rlocked.value())
            {
                rlocked.value()->receive(buffer);
                return true;
            }
            return false;
        }

        void hangup();
    };

    enum class type
    {
        system,
        console,
        serial,
        pty
    };

    enum class subtype
    {
        none,
        syscons,
        tty,
        pty_master,
        pty_slave
    };

    enum flag
    {
        none = 0,
        unnumbered = (1 << 0),
        dynamic = (1 << 1)
    };

    struct driver
    {
        const std::string driver_name;
        const std::string name;
        const std::size_t name_base;

        const std::uint32_t major;
        const std::uint32_t minor_start;
        const std::uint32_t num_devices;

        const flag flags;

        const type typ;
        const subtype subtyp;

        const ktermios init_termios;

        driver *other;
        lib::locker<
            lib::map::flat_hash<
                std::uint32_t,
                std::shared_ptr<instance>
            >, lib::mutex
        > instances;

        lib::intrusive_list_hook<driver> hook;

        driver(std::string_view driver_name, std::string_view name, std::size_t name_base,
            std::uint32_t major, std::uint32_t minor_start, std::uint32_t num_devices, flag flags,
            type typ, subtype subtyp, const ktermios &init_termios)
            : driver_name { driver_name }, name { name }, name_base { name_base },
              major { major }, minor_start { minor_start }, num_devices { num_devices }, flags { flags },
              typ { typ }, subtyp { subtyp },
              init_termios { init_termios } { }

        virtual ~driver() = default;

        virtual std::shared_ptr<instance> create_instance(std::uint32_t minor) = 0;
        virtual void destroy_instance(std::shared_ptr<instance> inst) = 0;

        // called from instance
        virtual lib::expect<int> ioctl(instance *inst, std::uint64_t request, lib::uptr_or_addr argp)
        {
            lib::bug_on(!inst);
            lib::unused(request, argp);
            return std::unexpected { lib::err::inappropriate_ioctl };
        }
    };

    void register_driver(driver *drv);
} // export namespace fs::dev::tty
