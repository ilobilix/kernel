// Copyright (C) 2024-2026  ilobilo

module drivers.fs.dev.tty;

import drivers.fs.devtmpfs;
import system.memory.virt;
import system.scheduler;
import system.vfs;
import system.vfs.dev;
import arch;
import lib;
import std;

import magic_enum;
import fmt;

namespace fs::dev::tty
{
    using namespace vfs::dev;
    namespace
    {
        constexpr bool debug = false;

        lib::intrusive_list<driver, &driver::hook> drivers;

        driver *get_driver(dev_t rdev)
        {
            const auto maj = major(rdev);
            const auto min = minor(rdev);

            if (const auto it = drivers.find_if([&](const auto &drv) {
                const auto minor_end = drv.minor_start + drv.num_devices;
                return drv.major == maj && drv.minor_start <= min && min < minor_end;
            }); it != drivers.end())
                return it.value();

            return nullptr;
        }

        lib::expect<void> generic_open(std::shared_ptr<vfs::file> file, std::shared_ptr<instance> inst, int flags, bool inst_opened)
        {
            if (!inst_opened)
            {
                if (const auto ret = inst->open(file); !ret)
                    return ret;
            }

            const auto proc = sched::proc_for(file->pid);
            lib::bug_on(!proc);

            const bool noctty = !(flags & vfs::o_noctty) ||
                (inst->drv->major == 5 && inst->minor == 0) || // tty
                (inst->drv->major == 5 && inst->minor == 1) || // console
                (inst->drv->typ == type::pty && inst->drv->subtyp == subtype::pty_master);

            if (noctty && (proc->pid == proc->sid)) // is leader
            {
                auto locked = inst->ctrl.lock();
                if (locked->sid == 0)
                {
                    const auto sess = sched::session_for(proc->sid);
                    lib::bug_on(!sess);

                    auto ctty_locked = sess->controlling_tty.lock();
                    if (!ctty_locked.value())
                    {
                        ctty_locked.value() = inst;

                        locked->pgid = proc->pgid;
                        locked->sid = proc->sid;
                    }
                }
            }

            return { };
        }

        lib::expect<void> generic_close(std::shared_ptr<vfs::file> file, std::shared_ptr<instance> inst)
        {
            lib::unused(file);

            if (const auto ret = inst->close(); !ret)
                return ret;

            if (auto locked = inst->ctrl.lock(); locked->sid)
            {
                lib::bug_on(!locked->pgid);
                if (const auto session = sched::session_for(locked->sid))
                {
                    auto ctty = session->controlling_tty.lock();
                    if (ctty.value() == inst)
                        ctty.value() = nullptr;
                }
                locked->sid = 0;
                locked->pgid = 0;
            }
            return { };
        }
    } // namespace

    default_ldisc::default_ldisc(instance *inst) : line_discipline { inst },
        raw_buffer { }, raw_sem { }, in_buffer { }, in_sem { },
        out_buffer { }, out_sem { }, stopped { false },
        worker_thread { }, should_work { false }, hung_sem { } { }

    void default_ldisc::open()
    {
        should_work.store(true, std::memory_order_relaxed);
        worker_thread = sched::spawn(
            reinterpret_cast<std::uintptr_t>(worker),
            reinterpret_cast<std::uintptr_t>(this), -10
        );
        lib::bug_on(!worker_thread);
    }

    void default_ldisc::hangup()
    {
        in_sem.signal_all();
        out_sem.signal_all();
        raw_sem.signal_all();

        in_poll_wq.wake_all();
        out_poll_wq.wake_all();
    }

    default_ldisc::~default_ldisc()
    {
        if (!should_work.load(std::memory_order_relaxed))
        {
            lib::bug_on(worker_thread != nullptr);
            return;
        }

        if constexpr (debug)
            lib::debug("tty: stopping worker thread in ({}, {})", inst->drv->major, inst->minor);

        should_work.store(false, std::memory_order_relaxed);
        raw_sem.signal();
        if (inst->hung_up.load(std::memory_order_relaxed))
            hung_sem.signal();
        while (!should_work.load(std::memory_order_relaxed)) // ehhhhhh
            sched::yield();
    }

    bool default_ldisc::output_append(const ktermios &termios, char chr)
    {
        using enum ktermios::oflag;

        if (!(termios.c_oflag & opost))
            return out_buffer.push(chr).first;

        if (chr == '\n' && (termios.c_oflag & onlcr))
        {
            std::array data { '\r', '\n' };
            return out_buffer.push(data).first;
        }

        if (chr == '\r' && (termios.c_oflag & onlret))
            return true;

        if (termios.c_oflag & olcuc)
            chr = std::toupper(chr);

        return out_buffer.push(chr).first;
    }

    void default_ldisc::output_flush()
    {
        if (stopped.load(std::memory_order_relaxed))
            return;

        while (const auto max_chars = inst->can_transmit())
        {
            const std::size_t size = std::min(max_chars, buffer_size);
            lib::membuffer buffer { size };

            const auto num_chars = out_buffer.pop(
                std::span {
                    reinterpret_cast<char *>(buffer.data()),
                    std::min(max_chars, size)
                }
            );

            if (num_chars == 0)
                break;

            out_sem.signal_all();
            out_poll_wq.wake_all();

            auto span = std::span { buffer.data(), num_chars };
            const auto res = inst->transmit(std::move(span));
            // TODO: error?
            if (res != num_chars)
                lib::warn("tty: could not transmit {} characters (got {})", num_chars, res);
        }
    }

    [[noreturn]]
    void default_ldisc::worker(default_ldisc *self)
    {
        // based on https://github.com/klange/toaruos/blob/master/kernel/vfs/tty.c

        if constexpr (debug)
        {
            lib::debug(
                "tty: started worker thread in ({}, {})",
                self->inst->drv->major, self->inst->minor
            );
        }

        using enum ktermios::iflag;
        using enum ktermios::oflag;
        // using enum ktermios::cflag;
        // using enum ktermios::baud;
        using enum ktermios::lflag;
        using enum ktermios::cc;

        lib::bug_on(!self || !self->inst);

        const auto is_control = [](char chr)
        {
            return chr < ' ' || chr == 0x7F;
        };

        ktermios termios;
        const auto echo_out = [&](char chr)
        {
            self->output_append(termios, chr);
        };

        const auto visual_erase = [&](char chr, bool do_echo)
        {
            if ((termios.c_lflag & echo) && do_echo)
            {
                for (std::size_t i = 0; i < (is_control(chr) ? 2 : 1); i++)
                {
                    echo_out('\b');
                    echo_out(' ');
                    echo_out('\b');
                }
            }
        };

        lib::bug_on(sched::this_thread() != self->worker_thread);

        bool next_is_verbatim = false;
        while (true)
        {
            if (!self->should_work.load(std::memory_order_relaxed))
            {
                if constexpr (debug)
                {
                    lib::debug(
                        "tty: worker thread in ({}, {}) got told to kys, day ruined",
                        self->inst->drv->major, self->inst->minor
                    );
                }

                self->should_work.store(true, std::memory_order_relaxed);
                sched::this_thread()->status = sched::status::killed;
                sched::yield();
                std::unreachable();
            }

            if (self->inst->hung_up.load(std::memory_order_relaxed))
            {
                if constexpr (debug)
                {
                    lib::debug(
                        "tty: hung up! worker thread in ({}, {}) is waiting for sweet release of death",
                        self->inst->drv->major, self->inst->minor
                    );
                }
                // presumably SIGHUP will be sent to everything that has this tty open
                // and once said ttys are closed, this ldisc will be destructed and worker - killed
                self->hung_sem.wait();
                continue;
            }

            auto ret = self->raw_buffer.pop();
            if (!ret.has_value())
            {
                self->output_flush();
                self->raw_sem.wait();
                continue;
            }
            auto chr = static_cast<char>(ret.value());

            termios = self->inst->termios.read_lock().value();
            if (next_is_verbatim)
            {
                next_is_verbatim = false;
                if (!self->in_buffer.lock()->push(chr))
                {
                    echo_out('\a');
                    continue;
                }

                if (termios.c_lflag & echo)
                {
                    if ((termios.c_lflag & echoctl) && is_control(chr))
                    {
                        echo_out('^');
                        echo_out((chr + '@') % 128);
                    }
                    else echo_out(chr);
                }
                continue;
            }

            if (termios.c_lflag & isig)
            {
                // TODO: signals
            }

            if (termios.c_iflag & ixon)
            {
                if (chr == termios.c_cc[vstop])
                {
                    self->stopped.store(true, std::memory_order_relaxed);
                    continue;
                }
                else if (chr == termios.c_cc[vstart])
                {
                    self->stopped.store(false, std::memory_order_relaxed);
                    continue;
                }
            }

            if ((termios.c_lflag & ixany) && self->stopped.load(std::memory_order_relaxed))
                self->stopped.store(false, std::memory_order_relaxed);

            if (termios.c_iflag & istrip)
                chr &= 0x7F;

            if (termios.c_iflag & igncr && chr == '\r')
                continue;

            if (termios.c_iflag & inlcr && chr == '\n')
                chr = '\r';
            else if (termios.c_iflag & icrnl && chr == '\r')
                chr = '\n';

            if (termios.c_iflag & iuclc)
                chr = std::tolower(chr);

            if (termios.c_lflag & icanon)
            {
                if (chr == termios.c_cc[vlnext] && (termios.c_lflag & iexten))
                {
                    next_is_verbatim = true;
                    echo_out('^');
                    echo_out('\b');
                    continue;
                }

                if (chr == termios.c_cc[vkill])
                {
                    {
                        auto in_locked = self->in_buffer.lock();
                        while (auto erased = in_locked->pop_last())
                            visual_erase(erased.value(), termios.c_lflag & echok);
                    }

                    if ((termios.c_lflag & echo) && !(termios.c_lflag & echok))
                    {
                        if ((termios.c_lflag & echoctl) && is_control(chr))
                        {
                            echo_out('^');
                            echo_out((chr + '@') % 128);
                        }
                        else echo_out(chr);
                    }
                    continue;
                }

                if (chr == termios.c_cc[verase])
                {
                    std::optional<char> erased;
                    {
                        auto in_locked = self->in_buffer.lock();
                        erased = in_locked->pop_last();
                    }

                    if (erased)
                        visual_erase(erased.value(), termios.c_lflag & echoe);

                    if ((termios.c_lflag & echo) && !(termios.c_lflag & echoe))
                    {
                        if ((termios.c_lflag & echoctl) && is_control(chr))
                        {
                            echo_out('^');
                            echo_out((chr + '@') % 128);
                        }
                        else echo_out(chr);
                    }
                    continue;
                }

                if (chr == termios.c_cc[vwerase] && (termios.c_lflag & iexten))
                {
                    {
                        auto in_locked = self->in_buffer.lock();

                        while (in_locked->peek() == ' ')
                        {
                            in_locked->erase();
                            visual_erase(' ', termios.c_lflag & echoe);
                        }

                        for (char chr = in_locked->peek(); chr != ' ' && chr != '\0'; )
                        {
                            auto erased = in_locked->pop_last();
                            visual_erase(erased.value(), termios.c_lflag & echoe);
                            chr = in_locked->peek();
                        }
                    }

                    if ((termios.c_lflag & echo) && !(termios.c_lflag & echoe))
                    {
                        if ((termios.c_lflag & echoctl) && is_control(chr))
                        {
                            echo_out('^');
                            echo_out((chr + '@') % 128);
                        }
                        else echo_out(chr);
                    }
                    continue;
                }

                const bool is_eol = (chr == '\n' || (termios.c_cc[veol] && chr == termios.c_cc[veol]));
                {
                    auto in_locked = self->in_buffer.lock();
                    if (chr == termios.c_cc[veof])
                    {
                        // if (!in_locked->push(chr))
                        // {
                        //     echo_out('\a');
                        //     continue;
                        // }

                        in_locked->commit();
                        self->in_sem.signal_all();
                        self->in_poll_wq.wake_all();
                        continue;
                    }

                    if (!in_locked->push(chr))
                    {
                        echo_out('\a');
                        continue;
                    }

                    if (is_eol)
                    {
                        in_locked->commit();
                        self->in_sem.signal_all();
                        self->in_poll_wq.wake_all();
                    }
                }

                if (termios.c_lflag & echo)
                {
                    if ((termios.c_lflag & echoctl) && is_control(chr) && chr != '\n')
                    {
                        echo_out('^');
                        echo_out((chr + '@') % 128);
                    }
                    else echo_out(chr);
                }

                if (is_eol)
                {
                    if (!(termios.c_lflag & echo) && (termios.c_lflag & echonl))
                        echo_out(chr);
                    continue;
                }
            }
            else // raw
            {
                auto in_locked = self->in_buffer.lock();
                if (!in_locked->full())
                {
                    in_locked->push(chr);
                    self->in_sem.signal_all();
                    self->in_poll_wq.wake_all();

                    if (termios.c_lflag & echo)
                    {
                        if ((termios.c_lflag & echoctl) && is_control(chr) && chr != '\n')
                        {
                            echo_out('^');
                            echo_out((chr + '@') % 128);
                        }
                        else echo_out(chr);
                    }
                }
                else echo_out('\a');
            }
        }
    }

    void default_ldisc::receive(std::span<std::byte> buffer)
    {
        // drop characters if raw buffer is full
        if (raw_buffer.push(buffer).first)
            raw_sem.signal(true);
    }

    lib::expect<std::size_t> default_ldisc::read(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer)
    {
        using enum ktermios::lflag;
        using enum ktermios::cc;

        lib::bug_on(!inst);

        const auto size = buffer.size();
        if (size == 0)
            return 0;

        const bool nonblock = (file->flags & vfs::o_nonblock) != 0;
        const auto termios = inst->termios.read_lock().value();
        const bool is_cooked = (termios.c_lflag & icanon) != 0;

        const auto min = termios.vmin;
        const auto time = termios.vtime;

        const auto get_available = [&](const auto &in_locked)
        {
            return is_cooked
                ? (in_locked->cooked_head - in_locked->read_tail)
                : (in_locked->read_head - in_locked->read_tail);
        };

        const auto extract_char = [&](auto &in_locked)
        {
            auto chr = in_locked->data[in_locked->read_tail % in_buffer_t::cap];
            in_locked->read_tail++;
            return chr;
        };

        const auto copy = [&](auto &in_locked, std::size_t  available, std::size_t start_from)
        {
            std::size_t to_read = std::min(size - start_from, available);
            if (to_read == 0)
                return 0uz;

            lib::membuffer buf { to_read };
            for (std::size_t i = 0; i < to_read; i++)
                buf.data()[i] = static_cast<std::byte>(extract_char(in_locked));

            in_locked.unlock();
            lib::bug_on(!buffer.subspan(start_from, to_read).copy_from(buf.span()));
            in_locked.lock();

            return to_read;
        };

        // vtime resolution is 100ms
        const auto ms = time * 100;

        if (nonblock)
        {
            // if nonblock is set, return immediately
            // if no data is avalable, return -1 EAGAIN

            auto in_locked = in_buffer.lock();
            const auto available = get_available(in_locked);
            if (available == 0)
                return std::unexpected { lib::err::try_again };

            return copy(in_locked, available, 0);
        }
        else if (is_cooked)
        {
            lib::membuffer buf;
            std::size_t progress = 0;
            {
                auto in_locked = in_buffer.lock();
                auto available = get_available(in_locked);
                while (available == 0)
                {
                    if (inst->hung_up.load(std::memory_order_relaxed))
                        return 0;

                    in_locked.unlock();
                    in_sem.wait();
                    in_locked.lock();
                    available = get_available(in_locked);
                }

                const auto to_read = std::min(size, available);
                buf.allocate(to_read);

                while (progress < to_read)
                {
                    auto chr = extract_char(in_locked);
                    if (chr == termios.c_cc[veof])
                        break;

                    buf.data()[progress] = static_cast<std::byte>(chr);
                    progress++;

                    if (chr == '\n' || (termios.c_cc[veol] && chr == termios.c_cc[veol]))
                        break;
                }
            }

            if (progress > 0)
            {
                lib::bug_on(
                    !buffer.subspan(0, progress).copy_from(
                        buf.span().subspan(0, progress)
                    )
                );
            }

            return progress;
        }
        else // raw
        {
            if (min == 0 && time == 0)
            {
                // if data is available, return immediately
                // otherwise return 0

                auto in_locked = in_buffer.lock();
                const auto available = get_available(in_locked);
                if (available == 0)
                    return 0;

                return copy(in_locked, available, 0);
            }
            else if (min > 0 && time == 0)
            {
                // block until at least vmin bytes are available

                auto in_locked = in_buffer.lock();
                auto available = get_available(in_locked);
                while (available < min)
                {
                    if (inst->hung_up.load(std::memory_order_relaxed))
                        return 0;

                    in_locked.unlock();
                    in_sem.wait();
                    in_locked.lock();
                    available = get_available(in_locked);
                }

                return copy(in_locked, available, 0);
            }
            else if (min == 0 && time > 0)
            {
                // return when at least one byte is available
                // if timer expires, return 0

                auto in_locked = in_buffer.lock();
                auto available = get_available(in_locked);
                if (available == 0)
                {
                    if (inst->hung_up.load(std::memory_order_relaxed))
                        return 0;

                    in_locked.unlock();
                    in_sem.wait_for(ms);
                    in_locked.lock();
                    available = get_available(in_locked);
                    if (available == 0)
                        return 0;
                }

                return copy(in_locked, available, 0);
            }
            else if (min > 0 && time > 0)
            {
                // wait until at least one byte is available
                // timer is started after each byte received
                // return when:
                //    vmin bytes received or
                //    buffer.size() bytes received or
                //    timer expired

                auto in_locked = in_buffer.lock();
                auto available = get_available(in_locked);
                while (available == 0)
                {
                    if (inst->hung_up.load(std::memory_order_relaxed))
                        return 0;

                    in_locked.unlock();
                    in_sem.wait();
                    in_locked.lock();
                    available = get_available(in_locked);
                }

                std::size_t progress = 0;
                while (progress < size)
                {
                    progress += copy(in_locked, available, progress);

                    if (progress >= min || progress >= size)
                        return progress;

                    available = get_available(in_locked);
                    while (available == 0)
                    {
                        if (inst->hung_up.load(std::memory_order_relaxed))
                            return progress;

                        in_locked.unlock();
                        bool interrupted = in_sem.wait_for(ms);
                        in_locked.lock();

                        if (!interrupted && available == 0) // expired
                            return progress;

                        available = get_available(in_locked);
                    }
                }
                return progress;
            }
        }
        std::unreachable();
    }

    lib::expect<std::size_t> default_ldisc::write(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer)
    {
        lib::bug_on(!inst);

        if (inst->hung_up.load(std::memory_order_relaxed))
            return std::unexpected { lib::err::io_error };

        const auto size = buffer.size();
        if (size == 0)
            return 0;

        // TODO: termios flags

        const bool nonblock = (file->flags & vfs::o_nonblock) != 0;
        const auto termios = inst->termios.read_lock().value();

        std::size_t progress = 0;
        while (progress < size)
        {
            const auto len = std::min(buffer_size, size - progress);
            lib::membuffer buf { len };
            buffer.subspan(progress, len).copy_to(buf.span());

            for (std::size_t i = 0; i < len; i++)
            {
                again:
                if (output_append(termios, static_cast<char>(buf.data()[i])))
                {
                    progress++;
                    continue;
                }

                // output buffer is full, try to flush it
                output_flush();

                if (output_append(termios, static_cast<char>(buf.data()[i])))
                {
                    progress++;
                    continue;
                }

                // hardware buffer is full and can't transmit
                if (nonblock)
                    return std::unexpected { lib::err::try_again };

                out_sem.wait();
                if (inst->hung_up.load(std::memory_order_relaxed))
                    return std::unexpected { lib::err::io_error };
                goto again;
            }
        }

        if (progress > 0)
            output_flush();

        return progress;
    }

    lib::expect<int> default_ldisc::ioctl(std::uint64_t request, lib::uptr_or_addr argp)
    {
        switch (request)
        {
            case tiocgpgrp:
                if (!argp.write(inst->ctrl.lock()->pgid))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tiocspgrp:
                if (!argp.read(inst->ctrl.lock()->pgid))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tiocgwinsz:
                if (!argp.write(inst->winsize.read_lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tiocswinsz:
                if (!argp.read(inst->winsize.write_lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tcgets2:
                if (!argp.write(inst->termios.read_lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tcsetsw2:
                // TODO: allow the output buffer to drain
                if (!argp.read(inst->termios.write_lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
        }
        return std::unexpected { lib::err::inappropriate_ioctl };
    }

    lib::expect<std::uint16_t> default_ldisc::poll(vfs::poll_table *pt)
    {
        using enum vfs::pollevents;
        lib::bug_on(!inst);

        std::uint16_t mask = 0;
        if (pt != nullptr)
        {
            pt->queue_wait(&in_poll_wq);
            pt->queue_wait(&out_poll_wq);
        }

        if (inst->hung_up.load(std::memory_order_relaxed))
        {
            mask |= (pollhup | pollin | pollout);
            return mask;
        }

        const auto termios = inst->termios.read_lock().value();
        const bool is_cooked = (termios.c_lflag & ktermios::lflag::icanon) != 0;

        std::size_t available = 0;
        {
            auto in_locked = in_buffer.lock();
            available = is_cooked
                ? (in_locked->cooked_head - in_locked->read_tail)
                : (in_locked->read_head - in_locked->read_tail);
        }

        if (available > 0)
            mask |= pollin;

        if (!out_buffer.full())
            mask |= pollout;

        return mask;
    }

    instance::instance(driver *drv, std::uint32_t minor, std::unique_ptr<line_discipline> ldisc)
        : drv { drv }, minor { minor }, ref { 0 }, hung_up { false }, ldisc { std::move(ldisc) },
          termios { drv->init_termios }, winsize { winsize::standard() }, ctrl { }
    { lib::bug_on(drv == nullptr); }

    lib::expect<int> instance::ioctl(std::uint64_t request, lib::uptr_or_addr argp)
    {
        const auto res = ldisc.read_lock()->get()->ioctl(request, argp);
        if (res.has_value() || (res.error() != lib::err::inappropriate_ioctl))
            return res;

        lib::bug_on(!drv);
        return drv->ioctl(this, request, argp);
    }

    lib::expect<std::uint16_t> instance::poll(vfs::poll_table *pt)
    {
        return ldisc.read_lock()->get()->poll(pt);
    }

    void instance::hangup()
    {
        if constexpr (debug)
            lib::debug("tty: hangup in ({}, {})", drv->major, minor);

        if (hung_up.exchange(true))
            return;

        // auto ctrl_locked = ctrl.lock();
        // TODO: send sighup to session leader (and sigcont)

        auto ldisc_locked = ldisc.read_lock();
        ldisc_locked.value()->hangup();
    }

    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags) override
        {
            lib::bug_on(!file || file->private_data != nullptr);
            lib::bug_on(!file->path.dentry || !file->path.dentry->inode);

            const auto rdev = file->path.dentry->inode->stat.st_rdev;
            auto drv = get_driver(rdev);
            if (!drv)
                return std::unexpected { lib::err::no_such_device };

            std::shared_ptr<instance> inst;
            {
                auto locked = drv->instances.lock();
                if (auto it = locked->find(minor(rdev)); it != locked->end())
                {
                    // found an already open instance
                    inst = it->second;
                    if (const auto ret = generic_open(file, inst, flags, true); !ret)
                        return ret;
                    inst->ref.fetch_add(1, std::memory_order_acq_rel);
                }
                else
                {
                    inst = drv->create_instance(minor(rdev));
                    if (!inst)
                        return std::unexpected { lib::err::no_such_device };

                    if (const auto ret = generic_open(file, inst, flags, false); !ret)
                    {
                        drv->destroy_instance(inst);
                        return ret;
                    }
                    inst->ref.store(1, std::memory_order_relaxed);
                    locked->emplace(minor(rdev), inst);
                    inst->ldisc.read_lock()->get()->open();
                }
            }
            file->private_data = inst;

            if constexpr (debug)
                lib::debug("tty: opened ({}, {}) for pid {}", major(rdev), minor(rdev), file->pid);
            return { };
        }

        lib::expect<void> close(std::shared_ptr<vfs::file> file) override
        {
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);

            const auto prev = inst->ref.fetch_sub(1, std::memory_order_acq_rel);
            lib::bug_on(prev == 0);
            if (prev == 1)
            {
                const auto drv = inst->drv;
                lib::bug_on(drv == nullptr);
                {
                    auto locked = drv->instances.lock();
                    // someone else could have opened it again
                    if (inst->ref.load(std::memory_order_acquire) != 0)
                        return { };

                    if (const auto ret = generic_close(file, inst); !ret)
                    {
                        // can't even close ttys properly smh
                        inst->ref.fetch_add(1, std::memory_order_relaxed);
                        return ret;
                    }
                    lib::bug_on(!locked->erase(inst->minor));
                }
                drv->destroy_instance(inst);
            }
            file->private_data.reset();

            if constexpr (debug)
            {
                const auto rdev = file->path.dentry->inode->stat.st_rdev;
                lib::debug("tty: closed ({}, {}) for pid {}", major(rdev), minor(rdev), file->pid);
            }
            return { };
        }

        lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->read(std::move(file), buffer);
        }

        lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->write(std::move(file), buffer);
        }

        lib::expect<int> ioctl(std::shared_ptr<vfs::file> file, std::uint64_t request, lib::uptr_or_addr argp) override
        {
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->ioctl(request, argp);
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return { };
        }

        lib::expect<std::uint16_t> poll(std::shared_ptr<vfs::file> file, vfs::poll_table *pt) override
        {
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->poll(pt);
        }
    };

    struct current_ops : vfs::ops
    {
        static std::shared_ptr<current_ops> singleton()
        {
            static auto instance = std::make_shared<current_ops>();
            return instance;
        }

        lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags) override
        {
            lib::bug_on(!file || file->private_data != nullptr);
            lib::unused(flags);

            const auto proc = sched::proc_for(file->pid);
            lib::bug_on(!proc);

            auto sess = sched::session_for(proc->sid);
            lib::bug_on(!sess);

            auto ctty = sess->controlling_tty.lock();
            if (!ctty.value())
                return std::unexpected { lib::err::invalid_device_or_address };

            // it's already open so just increment the ref count
            ctty.value()->ref.fetch_add(1, std::memory_order_acq_rel);
            file->private_data = ctty.value();

            if constexpr (debug)
                lib::debug("tty: opened (5, 0) for pid {}", file->pid);
            return { };
        }

        lib::expect<void> close(std::shared_ptr<vfs::file> file) override
        {
            return tty::ops::singleton()->close(std::move(file));
        }

        lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->read(std::move(file), buffer);
        }

        lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->write(std::move(file), buffer);
        }

        lib::expect<int> ioctl(std::shared_ptr<vfs::file> file, std::uint64_t request, lib::uptr_or_addr argp) override
        {
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->ioctl(request, argp);
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return { };
        }

        lib::expect<std::uint16_t> poll(std::shared_ptr<vfs::file> file, vfs::poll_table *pt) override
        {
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->poll(pt);
        }
    };

    void register_driver(driver *drv)
    {
        lib::bug_on(!drv);
        lib::debug("tty: registering driver '{}'", drv->driver_name);
        drivers.push_back(drv);

        if (drv->flags & dynamic)
            return;

        const auto add_one = [&](std::size_t idx, std::uint32_t minor)
        {
            using namespace vfs::dev;
            register_dev_ops(makedev(drv->major, minor), ops::singleton());

            std::string name;
            if (drv->typ == type::pty)
            {
                lib::bug_on(drv->name_base < 0);
                static const char ptychar[] = "pqrstuvwxyzabcde";
                const auto i = idx + drv->name_base;
                name = fmt::format("{}{}{}",
                    drv->subtyp == subtype::pty_slave ? "tty" : drv->name,
                    ptychar[i >> 4 & 0xF], i & 0xF
                );
            }
            else if (!(drv->flags & unnumbered))
                name = fmt::format("{}{}", drv->name, idx + drv->name_base);
            else
                name = drv->name;

            auto ret = vfs::create(
                std::nullopt, "/dev/" + name, stat::s_ifchr | 0666,
                makedev(drv->major, minor)
            );

            if (!ret.has_value())
            {
                lib::error(
                    "tty: could not create '{}': {}",
                    name, magic_enum::enum_name(ret.error())
                );
            }

            if constexpr (debug)
                lib::debug("tty: created ({}, {}) as '{}'", drv->major, minor, name);
        };

        for (std::size_t i = 0; i < drv->num_devices; i++)
            add_one(i, drv->minor_start + i);
    }

    // TODO: /dev/console

    lib::initgraph::task tty_task
    {
        "vfs.dev.tty.current.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { devtmpfs::mounted_stage() },
        [] {
            // TODO: /dev/tty driver instead of just ops?
            register_dev_ops(makedev(5, 0), current_ops::singleton());

            if (const auto ret = vfs::create(std::nullopt, "/dev/tty", stat::s_ifchr | 0666, makedev(5, 0)); !ret)
            {
                lib::panic(
                    "tty: failed to create '/dev/tty': {}",
                    magic_enum::enum_name(ret.error())
                );
            }
        }
    };
} // namespace fs::dev::tty