// Copyright (C) 2024-2026  ilobilo

module drivers.fs.dev.tty;

import drivers.fs.devtmpfs;
import system.memory.virt;
import system.vfs.dev;
import arch;
import fmt;

namespace fs::dev::tty
{
    using namespace vfs::dev;
    namespace
    {
        constexpr bool debug = false;

        lib::intrusive_list<driver, &driver::hook> drivers;

        struct console_target_t
        {
            driver *drv = nullptr;
            std::uint32_t minor = 0;
        };
        lib::locker<console_target_t, sched::mutex> console_target;

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

        lib::expect<void> generic_open(
            std::shared_ptr<vfs::file> file, std::shared_ptr<instance> inst,
            int flags, pid_t pid, bool inst_opened
        )
        {
            if (const auto ret = inst->permit_open(file); !ret)
                return ret;

            if (!inst_opened)
            {
                if (const auto ret = inst->open(file); !ret)
                    return ret;
            }

            const auto proc = sched::get_process(pid);
            lib::bug_on(!proc);

            const bool noctty = !(flags & vfs::o_noctty) ||
                (inst->drv->major == 5 && inst->minor == 0) || // tty
                (inst->drv->major == 5 && inst->minor == 1) || // console
                (inst->drv->typ == type::pty && inst->drv->subtyp == subtype::pty_master);

            if (noctty && (proc->pid == proc->session->sid)) // is leader
            {
                auto locked = inst->ctrl.lock();
                if (locked->session.use_count() == 0)
                {
                    auto ctty_locked = proc->session->ctty.lock();
                    if (!ctty_locked.value())
                    {
                        ctty_locked.value() = inst;

                        locked->group = proc->group;
                        locked->session = proc->session;
                    }
                }
            }

            return { };
        }

        lib::expect<void> generic_close(vfs::file &file, std::shared_ptr<instance> inst)
        {
            lib::unused(file);
            return inst->close();
        }

        lib::expect<std::shared_ptr<instance>> open_or_create(
            std::shared_ptr<vfs::file> file, driver *drv, std::uint32_t min,
            int flags, pid_t pid)
        {
            auto locked = drv->instances.lock();
            if (auto it = locked->find(min); it != locked->end())
            {
                auto inst = it->second;
                if (const auto ret = generic_open(file, inst, flags, pid, true); !ret)
                    return std::unexpected { ret.error() };
                inst->ref.fetch_add(1, std::memory_order_acq_rel);
                return inst;
            }

            auto inst = drv->create_instance(min);
            if (!inst)
                return std::unexpected { lib::err::no_such_device };

            if (const auto ret = generic_open(file, inst, flags, pid, false); !ret)
            {
                drv->destroy_instance(inst);
                return std::unexpected { ret.error() };
            }
            inst->ref.store(1, std::memory_order_relaxed);
            locked->emplace(min, inst);

            if (auto ld = inst->ldisc.lock().value())
                ld->open();
            return inst;
        }

        struct alias_ops : vfs::ops
        {
            bool seekable() const override { return false; }

            lib::expect<void> close(vfs::file &file) override
            {
                lib::bug_on(!file.private_data);
                const auto inst = std::static_pointer_cast<instance>(file.private_data);

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
                        if (inst->needs_close_erase())
                            locked->erase(inst->minor);
                    }
                    if (auto ldisc = inst->ldisc.lock().value())
                        ldisc->shutdown();
                    drv->destroy_instance(inst);
                }
                file.private_data.reset();

                if constexpr (debug)
                {
                    const auto rdev = file.path.dentry->inode->stat.st_rdev;
                    lib::debug("tty: closed ({}, {})", major(rdev), minor(rdev));
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
    } // namespace

    void instance::detach(sched::session_t *session)
    {
        std::shared_ptr<sched::group_t> fg_group;
        {
            auto locked = ctrl.lock();
            if (locked->session.lock().get() == session)
            {
                fg_group = locked->group.lock();
                locked->session.reset();
                locked->group.reset();
            }
        }

        {
            auto locked = session->ctty.lock();
            if (locked.value().get() == this)
                locked.value().reset();
        }

        if (fg_group)
        {
            fg_group->signal_all(sched::sighup);
            fg_group->signal_all(sched::sigcont);
        }
    }

    default_ldisc::default_ldisc(instance *inst) : line_discipline { inst },
        raw_buffer { }, raw_wq { }, in_buffer { }, in_wq { },
        out_buffer { }, out_wq { }, stopped { false },
        worker_thread { }, should_work { false }, shut_down { false }, hung_wq { } { }

    void default_ldisc::open()
    {
        should_work.store(true, std::memory_order_relaxed);
        worker_thread = sched::spawn(worker, this, -10);
    }

    void default_ldisc::shutdown()
    {
        if (shut_down.exchange(true, std::memory_order_acq_rel))
            return;

        if (!should_work.load(std::memory_order_relaxed))
            return;

        if constexpr (debug)
            lib::debug("tty: stopping worker thread in ({}, {})", inst->drv->major, inst->minor);

        should_work.store(false, std::memory_order_relaxed);

        while (!should_work.load(std::memory_order_relaxed))
        {
            raw_wq.wake_all();
            hung_wq.wake_all();
            sched::yield();
        }
    }

    void default_ldisc::hangup()
    {
        in_wq.wake_all();
        out_wq.wake_all();
        raw_wq.wake_all();
    }

    default_ldisc::~default_ldisc()
    {
        shutdown();
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

            out_wq.wake_all();

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

        lib::bug_on(sched::current_thread() != self->worker_thread);

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
                sched::thread_exit(0);
            }

            auto ret = self->raw_buffer.pop();
            if (!ret.has_value())
            {
                self->output_flush();

                if (self->inst->hung_up.load(std::memory_order_relaxed))
                {
                    if constexpr (debug)
                    {
                        lib::debug(
                            "tty: hung up! worker thread in ({}, {}) is waiting for sweet release of death",
                            self->inst->drv->major, self->inst->minor
                        );
                    }
                    self->hung_wq.wait();
                }
                else self->raw_wq.wait();
                continue;
            }
            auto chr = static_cast<char>(ret.value());

            termios = self->inst->termios.lock().value();
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
                int sig = 0;
                if (chr == termios.c_cc[vintr])
                    sig = sched::sigint;
                else if (chr == termios.c_cc[vquit])
                    sig = sched::sigquit;
                else if (chr == termios.c_cc[vsusp])
                    sig = sched::sigtstp;

                if (sig != 0)
                {
                    if ((termios.c_lflag & echo) && (termios.c_lflag & echoctl) && is_control(chr))
                    {
                        echo_out('^');
                        echo_out((chr + '@') % 128);
                    }

                    if (!(termios.c_lflag & noflsh))
                    {
                        {
                            auto in_locked = self->in_buffer.lock();
                            in_locked->read_tail = in_locked->read_head;
                            in_locked->cooked_head = in_locked->read_head;
                        }
                        while (self->raw_buffer.pop().has_value()) { }
                    }

                    std::shared_ptr<sched::group_t> fg;
                    {
                        auto locked = self->inst->ctrl.lock();
                        fg = locked->group.lock();
                    }
                    if (fg)
                        fg->signal_all(sig);
                    continue;
                }
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

            if ((termios.c_iflag & igncr) && chr == '\r')
                continue;

            if ((termios.c_iflag & inlcr) && chr == '\n')
                chr = '\r';
            else if ((termios.c_iflag & icrnl) && chr == '\r')
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
                        self->in_wq.wake_all();
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
                        self->in_wq.wake_all();
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
                    self->in_wq.wake_all();

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
            raw_wq.wake_all();
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
        const auto termios = inst->termios.lock().value();
        const bool is_cooked = (termios.c_lflag & icanon) != 0;

        const auto min = termios.c_cc[vmin];
        const auto time = termios.c_cc[vtime];

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

        const auto copy = [&](auto &in_locked, std::size_t available, std::size_t start_from)
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

        const auto pipeline_empty = [&] {
            return raw_buffer.empty() && inst->raw_buffer.empty();
        };

        if (nonblock)
        {
            // if nonblock is set, return immediately
            // if no data is avalable, return -1 EAGAIN

            auto in_locked = in_buffer.lock();
            const auto available = get_available(in_locked);
            if (available == 0)
            {
                if (inst->hung_up.load(std::memory_order_relaxed) && pipeline_empty())
                    return 0;
                return std::unexpected { lib::err::try_again };
            }

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
                    if (inst->hung_up.load(std::memory_order_relaxed) && pipeline_empty())
                        return 0;

                    in_locked.unlock();
                    in_wq.wait();
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
                    if (inst->hung_up.load(std::memory_order_relaxed) && pipeline_empty())
                        return 0;

                    in_locked.unlock();
                    in_wq.wait();
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
                    if (inst->hung_up.load(std::memory_order_relaxed) && pipeline_empty())
                        return 0;

                    in_locked.unlock();
                    in_wq.wait(ms * 1'000'000);
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
                    if (inst->hung_up.load(std::memory_order_relaxed) && pipeline_empty())
                        return 0;

                    in_locked.unlock();
                    in_wq.wait();
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
                        bool interrupted = in_wq.wait(ms * 1'000'000);
                        in_locked.lock();

                        available = get_available(in_locked);
                        if (!interrupted && available == 0) // expired
                            return progress;
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
        const auto termios = inst->termios.lock().value();

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

                out_wq.wait();
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
        const auto apply_locked = [&](ktermios &tios, const ktermios &old)
        {
            const auto locked = inst->termios_locked.lock().value();
            tios.c_iflag = (tios.c_iflag & ~locked.c_iflag) | (old.c_iflag & locked.c_iflag);
            tios.c_oflag = (tios.c_oflag & ~locked.c_oflag) | (old.c_oflag & locked.c_oflag);
            tios.c_cflag = (tios.c_cflag & ~locked.c_cflag) | (old.c_cflag & locked.c_cflag);
            tios.c_lflag = (tios.c_lflag & ~locked.c_lflag) | (old.c_lflag & locked.c_lflag);
        };

        switch (request)
        {
            case kdgkbmode:
            {
                // TODO
                const auto mode = inst->kbmode.load(std::memory_order_relaxed);
                if (!argp.write(mode))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case kdskbmode:
            {
                // TODO
                const auto mode = static_cast<int>(argp.address());
                switch (mode)
                {
                    case 0x00: // K_RAW
                    case 0x01: // K_XLATE
                    case 0x02: // K_MEDIUMRAW
                    case 0x03: // K_UNICODE
                    case 0x04: // K_OFF
                        break;
                    default:
                        return std::unexpected { lib::err::invalid_argument };
                }
                inst->kbmode.store(mode, std::memory_order_relaxed);
                return 0;
            }
            case kdgkbtype:
            {
                // TODO: keyboard type
                constexpr std::uint8_t kb_101 = 0x02;
                if (!argp.write(kb_101))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case kdsigaccept:
            {
                // TODO: magic keys
                const auto sig = static_cast<int>(argp.address());
                if (sig < 1 || sig > static_cast<int>(sched::nsig) ||
                    sig == sched::sigkill || sig == sched::sigstop)
                    return std::unexpected { lib::err::invalid_argument };
                return 0;
            }
            case vt_getstate:
                // TODO
                return std::unexpected { lib::err::inappropriate_ioctl };
            case tiocgpgrp:
            {
                const auto proc = sched::current_process();

                auto locked = inst->ctrl.lock();
                if (locked->session.lock() != proc->session)
                    return std::unexpected { lib::err::inappropriate_ioctl };

                auto glocked = locked->group.lock();
                if (!glocked)
                    return std::unexpected { lib::err::inappropriate_ioctl };
                if (!argp.write(glocked->pgid))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case tiocgsid:
            {
                const auto proc = sched::current_process();

                auto locked = inst->ctrl.lock();
                const auto tty_session = locked->session.lock();
                if (!tty_session || tty_session != proc->session)
                    return std::unexpected { lib::err::inappropriate_ioctl };

                if (!argp.write(tty_session->sid))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case tiocspgrp:
            {
                pid_t pgid;
                if (!argp.read(pgid))
                    return std::unexpected { lib::err::invalid_address };
                if (pgid < 0)
                    return std::unexpected { lib::err::invalid_flags };

                const auto proc = sched::current_process();

                auto locked = inst->ctrl.lock();
                auto tty_session = locked->session.lock();
                if (!tty_session || tty_session != proc->session)
                    return std::unexpected { lib::err::inappropriate_ioctl };

                std::shared_ptr<sched::group_t> new_group;
                {
                    auto members = tty_session->members.lock();
                    auto it = members->find(pgid);
                    if (it == members->end())
                        return std::unexpected { lib::err::permission_denied };
                    new_group = it->second.lock();
                    if (!new_group)
                        return std::unexpected { lib::err::permission_denied };
                }
                locked->group = std::move(new_group);
                return 0;
            }
            case tiocgwinsz:
                if (!argp.write(inst->winsize.lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tiocswinsz:
                if (!argp.read(inst->winsize.lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tiocsctty:
            {
                const int force = static_cast<int>(argp.address());

                const auto proc = sched::current_process();
                if (proc->pid != proc->session->sid)
                    return std::unexpected { lib::err::not_permitted };

                auto locked = inst->ctrl.lock();
                if (auto existing = locked->session.lock())
                {
                    if (existing == proc->session)
                        return 0;

                    if (force != 1 || !sched::capable(sched::cap_t::sys_admin))
                        return std::unexpected { lib::err::permission_denied };

                    auto old_ctty = existing->ctty.lock();
                    if (old_ctty.value().get() == inst)
                        old_ctty.value().reset();

                    locked->session.reset();
                    locked->group.reset();
                }

                {
                    auto locked = proc->session->ctty.lock();
                    if (locked.value())
                        return std::unexpected { lib::err::permission_denied };
                    locked.value() = inst->shared_from_this();
                }

                locked->session = proc->session;
                locked->group = proc->group;
                return 0;
            }
            case tiocnotty:
            {
                const auto proc = sched::current_process();

                std::shared_ptr<sched::group_t> fg_group;
                {
                    auto locked = inst->ctrl.lock();
                    if (locked->session.lock() != proc->session)
                        return std::unexpected { lib::err::not_permitted };

                    if (proc->pid != proc->session->sid)
                        return 0;

                    fg_group = locked->group.lock();
                    locked->session.reset();
                    locked->group.reset();
                }

                {
                    auto ctty_locked = proc->session->ctty.lock();
                    if (ctty_locked.value().get() == inst)
                        ctty_locked.value().reset();
                }

                if (fg_group)
                {
                    fg_group->signal_all(sched::sighup);
                    fg_group->signal_all(sched::sigcont);
                }
                return 0;
            }
            case tcsbrk:
            {
                while (!out_buffer.empty() && !inst->hung_up.load(std::memory_order_relaxed))
                    sched::yield();
                if (argp.address() == 0)
                {
                    inst->break_ctl(true);
                    sched::sleep_for_ns(250'000'000);
                    inst->break_ctl(false);
                }
                return 0;
            }
            case tcxonc:
            {
                const auto termios = inst->termios.lock().value();
                switch (argp.address())
                {
                    case tcooff:
                        stopped.store(true, std::memory_order_relaxed);
                        break;
                    case tcoon:
                        stopped.store(false, std::memory_order_relaxed);
                        output_flush();
                        out_wq.wake_all();
                        break;
                    case tcioff:
                        output_append(termios, termios.c_cc[ktermios::cc::vstop]);
                        output_flush();
                        break;
                    case tcion:
                        output_append(termios, termios.c_cc[ktermios::cc::vstart]);
                        output_flush();
                        break;
                    default:
                        return std::unexpected { lib::err::invalid_flags };
                }
                return 0;
            }
            case tcgets:
            {
                const auto cur = inst->termios.lock().value();
                utermios utios {
                    .c_iflag = cur.c_iflag,
                    .c_oflag = cur.c_oflag,
                    .c_cflag = cur.c_cflag,
                    .c_lflag = cur.c_lflag,
                    .c_line = cur.c_line,
                    .c_cc = { }
                };
                std::memcpy(utios.c_cc, cur.c_cc, sizeof(utios.c_cc));
                if (!argp.write(utios))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case tcsets:
            case tcsetsw:
            case tcsetsf:
            {
                utermios utios;
                if (!argp.read(utios))
                    return std::unexpected { lib::err::invalid_address };

                if (request == tcsetsw || request == tcsetsf)
                {
                    // TODO: do it better
                    while (!out_buffer.empty() && !inst->hung_up.load(std::memory_order_relaxed))
                        sched::yield();
                }

                if (request == tcsetsf)
                {
                    auto in_locked = in_buffer.lock();
                    in_locked->read_tail = in_locked->read_head;
                    in_locked->cooked_head = in_locked->read_head;
                }

                auto wlocked = inst->termios.lock();
                const auto old = wlocked.value();

                wlocked->c_iflag = utios.c_iflag;
                wlocked->c_oflag = utios.c_oflag;
                wlocked->c_cflag = utios.c_cflag;
                wlocked->c_lflag = utios.c_lflag;
                wlocked->c_line = utios.c_line;
                std::memcpy(wlocked->c_cc, utios.c_cc, sizeof(utios.c_cc));
                apply_locked(wlocked.value(), old);

                using enum ktermios::lflag;
                if (!(old.c_lflag & icanon) && (wlocked->c_lflag & icanon))
                {
                    auto in_locked = in_buffer.lock();
                    in_locked->cooked_head = in_locked->read_head;
                }

                inst->set_termios(wlocked.value(), old);
                return 0;
            }
            case tcgets2:
                if (!argp.write(inst->termios.lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tcsets2:
            case tcsetsw2:
            case tcsetsf2:
            {
                ktermios ktios;
                if (!argp.read(ktios))
                    return std::unexpected { lib::err::invalid_address };

                if (request == tcsetsw2 || request == tcsetsf2)
                {
                    // TODO: do it better
                    while (!out_buffer.empty() && !inst->hung_up.load(std::memory_order_relaxed))
                        sched::yield();
                }

                if (request == tcsetsf2)
                {
                    auto in_locked = in_buffer.lock();
                    in_locked->read_tail = in_locked->read_head;
                    in_locked->cooked_head = in_locked->read_head;
                }

                auto wlocked = inst->termios.lock();
                const auto old = wlocked.value();
                wlocked.value() = ktios;
                apply_locked(wlocked.value(), old);

                using enum ktermios::lflag;
                if (!(old.c_lflag & ktermios::icanon) && (wlocked->c_lflag & ktermios::icanon))
                {
                    auto in_locked = in_buffer.lock();
                    in_locked->cooked_head = in_locked->read_head;
                }

                inst->set_termios(wlocked.value(), old);
                return 0;
            }
            case tcflsh:
            {
                switch (argp.address())
                {
                    case tciflush:
                    {
                        auto in_locked = in_buffer.lock();
                        in_locked->read_tail = in_locked->read_head;
                        in_locked->cooked_head = in_locked->read_head;
                        raw_buffer.clear();
                        break;
                    }
                    case tcoflush:
                        out_buffer.clear();
                        break;
                    case tcioflush:
                    {
                        {
                            auto in_locked = in_buffer.lock();
                            in_locked->read_tail = in_locked->read_head;
                            in_locked->cooked_head = in_locked->read_head;
                            raw_buffer.clear();
                        }
                        out_buffer.clear();
                        break;
                    }
                    default:
                        return std::unexpected { lib::err::invalid_flags };
                }
                return 0;
            }
            case tiocglcktrmios:
                if (!argp.write(inst->termios_locked.lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tiocslcktrmios:
            {
                if (!sched::capable(sched::cap_t::sys_admin))
                    return std::unexpected { lib::err::permission_denied };
                if (!argp.read(inst->termios_locked.lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            default:
                lib::error("tty: unhandled ioctl: 0x{:X}", request);
                break;
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
            pt->add(in_wq);
            pt->add(out_wq);
        }

        const bool hung_up = inst->hung_up.load(std::memory_order_relaxed);

        const auto termios = inst->termios.lock().value();
        const bool is_cooked = (termios.c_lflag & ktermios::lflag::icanon) != 0;

        std::size_t available = 0;
        {
            auto in_locked = in_buffer.lock();
            available = is_cooked
                ? (in_locked->cooked_head - in_locked->read_tail)
                : (in_locked->read_head - in_locked->read_tail);
        }

        if (hung_up || available > 0 || !raw_buffer.empty() || !inst->raw_buffer.empty())
            mask |= pollin;

        if (!hung_up && !out_buffer.full())
            mask |= pollout;

        if (hung_up)
            mask |= pollhup;

        return mask;
    }

    instance::instance(driver *drv, std::uint32_t minor, std::shared_ptr<line_discipline> ld)
        : drv { drv }, minor { minor }, ref { 0 }, hung_up { false }, kbmode { 0x01 /* K_XLATE */ },
          ldisc { ld }, termios { drv->init_termios }, termios_locked { ktermios { } },
          winsize { winsize::standard() }, ctrl { }, raw_buffer { }, raw_wq { },
          worker_thread { nullptr }, raw_should_work { ld != nullptr }
    {
        lib::bug_on(drv == nullptr);
        if (ld)
            worker_thread = sched::spawn(raw_worker, this, -10);
    }

    instance::~instance()
    {
        if (!raw_should_work.load(std::memory_order_relaxed))
        {
            lib::bug_on(worker_thread != nullptr);
            return;
        }

        raw_should_work.store(false, std::memory_order_relaxed);
        raw_wq.wake_one();

        while (!raw_should_work.load(std::memory_order_relaxed))
            sched::yield();
    }

    [[noreturn]]
    void instance::raw_worker(instance *self)
    {
        lib::bug_on(!self || sched::current_thread() != self->worker_thread);

        while (true)
        {
            if (!self->raw_should_work.load(std::memory_order_relaxed))
            {
                self->raw_should_work.store(true, std::memory_order_relaxed);
                sched::thread_exit(0);
            }

            std::array<std::byte, 64> chunk;
            const auto num = self->raw_buffer.pop(std::span { chunk });
            if (num == 0)
            {
                self->raw_wq.wait();
                continue;
            }

            auto ld = self->ldisc.lock().value();
            lib::bug_on(!ld);
            ld->receive(std::span { chunk.data(), num });
        }
    }

    lib::expect<int> instance::ioctl(std::uint64_t request, lib::uptr_or_addr argp)
    {
        auto ld = ldisc.lock().value();
        if (!ld)
            return std::unexpected { lib::err::io_error };
        const auto res = ld->ioctl(request, argp);
        if (res.has_value() || (res.error() != lib::err::inappropriate_ioctl))
            return res;

        lib::bug_on(!drv);
        return drv->ioctl(this, request, argp);
    }

    lib::expect<std::uint16_t> instance::poll(vfs::poll_table *pt)
    {
        auto ld = ldisc.lock().value();
        if (!ld)
            return std::unexpected { lib::err::io_error };
        return ld->poll(pt);
    }

    void instance::hangup()
    {
        if constexpr (debug)
            lib::debug("tty: hangup in ({}, {})", drv->major, minor);

        if (hung_up.exchange(true))
            return;

        std::shared_ptr<sched::session_t> session;
        std::shared_ptr<sched::group_t> fg_group;
        {
            auto locked = ctrl.lock();
            session = locked->session.lock();
            fg_group = locked->group.lock();
            locked->session.reset();
            locked->group.reset();
        }

        if (session)
        {
            auto locked = session->ctty.lock();
            if (locked.value().get() == this)
                locked.value().reset();
        }

        if (fg_group)
        {
            fg_group->signal_all(sched::sighup);
            fg_group->signal_all(sched::sigcont);
        }

        if (auto ld = ldisc.lock().value())
            ld->hangup();
    }

    struct ops : alias_ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags, pid_t pid) override
        {
            lib::bug_on(!file || file->private_data != nullptr);
            lib::bug_on(!file->path.dentry || !file->path.dentry->inode);

            const auto rdev = file->path.dentry->inode->stat.st_rdev;
            auto drv = get_driver(rdev);
            if (!drv)
                return std::unexpected { lib::err::no_such_device };

            auto res = open_or_create(file, drv, minor(rdev), flags, pid);
            if (!res)
                return std::unexpected { res.error() };
            file->private_data = std::move(*res);

            if constexpr (debug)
                lib::debug("tty: opened ({}, {}) for pid {}", major(rdev), minor(rdev), pid);
            return { };
        }
    };

    struct current_ops : alias_ops
    {
        static std::shared_ptr<current_ops> singleton()
        {
            static auto instance = std::make_shared<current_ops>();
            return instance;
        }

        lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags, pid_t pid) override
        {
            lib::bug_on(!file || file->private_data != nullptr);
            lib::unused(flags);

            const auto proc = sched::get_process(pid);
            lib::bug_on(!proc);

            auto ctty = proc->session->ctty.lock();
            if (!ctty.value())
                return std::unexpected { lib::err::invalid_device_or_address };

            // it's already open so just increment the ref count
            std::static_pointer_cast<instance>(ctty.value())
                ->ref.fetch_add(1, std::memory_order_acq_rel);
            file->private_data = ctty.value();

            if constexpr (debug)
                lib::debug("tty: opened (5, 0) for pid {}", pid);
            return { };
        }
    };

    struct console_ops : alias_ops
    {
        static std::shared_ptr<console_ops> singleton()
        {
            static auto instance = std::make_shared<console_ops>();
            return instance;
        }

        lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags, pid_t pid) override
        {
            lib::bug_on(!file || file->private_data != nullptr);

            driver *drv;
            std::uint32_t min;
            {
                auto locked = console_target.lock();
                drv = locked->drv;
                min = locked->minor;
            }
            if (!drv)
                return std::unexpected { lib::err::no_such_device };

            auto res = open_or_create(file, drv, min, flags, pid);
            if (!res)
                return std::unexpected { res.error() };
            file->private_data = std::move(*res);

            if constexpr (debug)
                lib::debug("tty: opened (5, 1) for pid {}", pid);
            return { };
        }
    };

    void set_console(driver *drv, std::uint32_t minor)
    {
        lib::bug_on(!drv);
        auto locked = console_target.lock();
        locked->drv = drv;
        locked->minor = minor;
    }

    void set_console(dev_t rdev)
    {
        auto drv = get_driver(rdev);
        if (!drv)
        {
            lib::warn(
                "tty: set_console: no tty driver for ({}, {})",
                major(rdev), minor(rdev)
            );
            return;
        }
        set_console(drv, minor(rdev));
    }

    void register_chrdev(dev_t rdev)
    {
        vfs::dev::register_dev_ops(rdev, ops::singleton());
    }

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
                    name, lib::error_name(ret.error())
                );
            }

            if constexpr (debug)
                lib::debug("tty: created ({}, {}) as '{}'", drv->major, minor, name);
        };

        lib::panic_if((drv->flags & unnumbered) && drv->num_devices > 1);
        for (std::size_t i = 0; i < drv->num_devices; i++)
            add_one(i, drv->minor_start + i);
    }

    lib::initgraph::task tty_task
    {
        "vfs.dev.tty.current.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { devtmpfs::registered_stage() },
        [] {
            register_dev_ops(makedev(5, 0), current_ops::singleton());
            if (const auto ret = fs::devtmpfs::create("tty", stat::s_ifchr | 0666, makedev(5, 0)); !ret)
            {
                lib::panic(
                    "tty: failed to create '/dev/tty': {}",
                    lib::error_name(ret.error())
                );
            }

            register_dev_ops(makedev(5, 1), console_ops::singleton());
            if (const auto ret = fs::devtmpfs::create("console", stat::s_ifchr | 0666, makedev(5, 1)); !ret)
            {
                lib::panic(
                    "tty: failed to create '/dev/console': {}",
                    lib::error_name(ret.error())
                );
            }
        }
    };
} // namespace fs::dev::tty
