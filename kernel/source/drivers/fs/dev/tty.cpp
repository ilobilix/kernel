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
    namespace
    {
        constexpr bool debug = false;

        lib::intrusive_list<driver, &driver::hook> drivers;

        driver *get_driver(dev_t major)
        {
            if (const auto it = drivers.find_if([major](const auto &drv) {
                return drv.major == major;
            }); it != drivers.end())
                return it.value();
            return nullptr;
        }

        bool generic_open(std::shared_ptr<vfs::file> self, std::shared_ptr<instance> inst, int flags)
        {
            if (auto locked = inst->ctrl.lock(); !locked->pgid || !locked->sid)
            {
                lib::bug_on(locked->pgid || locked->sid);

                const auto proc = sched::proc_for(self->pid);
                lib::bug_on(!proc);

                locked->pgid = proc->pgid;
                locked->sid = proc->sid;
            }

            if (!(flags & vfs::o_noctty))
            {
                const auto proc = sched::proc_for(self->pid);
                lib::bug_on(!proc);

                if (self->pid != proc->sid)
                    goto skip;

                const auto sess = sched::session_for(proc->sid);
                lib::bug_on(!sess);

                sess->controlling_tty.lock().value() = inst;
            }
            skip:

            return inst->open(self);
        }

        bool generic_close(std::shared_ptr<vfs::file> self, std::shared_ptr<instance> inst)
        {
            lib::unused(self);

            if (!inst->close())
                return false;

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
            return true;
        }
    } // namespace

    default_ldisc::default_ldisc(instance *inst) : line_discipline { inst },
        raw_buffer { }, raw_sem { }, in_buffer { }, in_sem { },
        out_buffer { }, out_sem { }, stopped { false }
    {
        sched::spawn(0, reinterpret_cast<std::uintptr_t>(worker), reinterpret_cast<std::uintptr_t>(this), -10);
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

        bool next_is_verbatim = false;
        while (true)
        {
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

                {
                    auto in_locked = self->in_buffer.lock();
                    if (chr == termios.c_cc[veof])
                    {
                        in_locked->commit();
                        self->in_sem.signal_all();
                        continue;
                    }

                    if (!in_locked->push(chr))
                    {
                        echo_out('\a');
                        continue;
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

                if (chr == '\n' || (termios.c_cc[veol] && chr == termios.c_cc[veol]))
                {
                    if (!(termios.c_lflag & echo) && (termios.c_lflag & echonl))
                        echo_out(chr);

                    auto in_locked = self->in_buffer.lock();
                    if (in_locked->push(chr))
                    {
                        in_locked->commit();
                        self->in_sem.signal_all();
                    }
                    else echo_out('\a');
                    continue;
                }
            }
            else // raw
            {
                auto in_locked = self->in_buffer.lock();
                if (!in_locked->full())
                {
                    in_locked->push(chr);
                    self->in_sem.signal(true);

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

    std::ssize_t default_ldisc::read(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer)
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

        // TODO: handle terminal disconnecting

        if (nonblock)
        {
            // if nonblock is set, return immediately
            // if no data is avalable, return -1 EAGAIN

            auto in_locked = in_buffer.lock();
            const auto available = get_available(in_locked);
            if (available == 0)
                return (errno = EAGAIN, -1);

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
                    in_locked.unlock();
                    in_sem.wait();
                    in_locked.lock();
                    available = get_available(in_locked);
                }

                std::size_t progress = 0;
                while (progress < std::min<std::size_t>(size, min))
                {
                    progress += copy(in_locked, available, progress);
                    available = get_available(in_locked);
                    while (available == 0)
                    {
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

    std::ssize_t default_ldisc::write(std::shared_ptr<vfs::file> file, lib::maybe_uspan<std::byte> buffer)
    {
        lib::bug_on(!inst);

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
                    return progress ?: (errno = EAGAIN, -1);

                out_sem.wait();
                goto again;
            }
        }

        if (progress > 0)
            output_flush();

        return progress;
    }

    instance::instance(driver *drv, std::uint32_t minor, std::unique_ptr<line_discipline> ldisc)
        : drv { drv }, minor { minor }, ref { 0 }, ldisc { std::move(ldisc) },
          termios { drv->init_termios }, winsize { }, ctrl { }
    { lib::bug_on(drv == nullptr); }

    struct test_driver : driver
    {
        struct test_instance : instance
        {
            std::size_t transmit(std::span<std::byte> buffer) override
            {
                const std::string_view str {
                    reinterpret_cast<const char *>(buffer.data()),
                    buffer.size_bytes()
                };
                lib::print("{}", str);
                return buffer.size_bytes();
            }

            std::size_t can_transmit() override
            {
                return std::numeric_limits<std::size_t>::max();
            }

            bool open(std::shared_ptr<vfs::file> self) override
            {
                lib::unused(self);
                return true;
            }

            bool close() override { return true; }

            int ioctl(unsigned long request, lib::uptr_or_addr argp) override
            {
                lib::unused(request, argp);
                return 0;
            }

            test_instance(driver *drv, std::uint32_t minor)
                : instance { drv, minor, std::make_unique<default_ldisc>(this) } { }
        };

        std::shared_ptr<instance> create_instance(std::uint32_t minor) override
        {
            if constexpr (debug)
                lib::debug("tty: creating test instance with minor {}", minor);
            return std::make_shared<test_instance>(this, minor);
        }

        void destroy_instance(std::shared_ptr<instance> inst) override
        {
            lib::unused(inst);
            if constexpr (debug)
                lib::debug("tty: destroying test instance with minor {}", inst->minor);
        }

        int ioctl(std::shared_ptr<instance> inst, unsigned long request, lib::uptr_or_addr argp) override
        {
            lib::bug_on(!inst);
            return inst->ioctl(request, argp);
        }

        test_driver() : driver { "tty-test", 4, 0, ktermios::standard() } { }
    };

    using namespace vfs::dev;

    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        bool open(std::shared_ptr<vfs::file> self, int flags) override
        {
            lib::bug_on(!self || self->private_data != nullptr);
            lib::bug_on(!self->path.dentry || !self->path.dentry->inode);

            const auto rdev = self->path.dentry->inode->stat.st_rdev;
            auto drv = get_driver(major(rdev));
            if (!drv)
                return (errno = ENODEV, false);

            std::shared_ptr<instance> inst;
            {
                auto locked = drv->instances.lock();
                if (auto it = locked->find(minor(rdev)); it != locked->end())
                {
                    // found an already open instance
                    inst = it->second;
                    inst->ref.fetch_add(1, std::memory_order_acq_rel);
                }
                else
                {
                    inst = drv->create_instance(minor(rdev));
                    if (!inst)
                        return (errno = ENODEV, false);

                    if (!generic_open(self, inst, flags))
                    {
                        drv->destroy_instance(inst);
                        return false;
                    }
                    inst->ref.store(1, std::memory_order_relaxed);
                    locked->emplace(minor(rdev), inst);
                }
            }
            self->private_data = inst;

            if constexpr (debug)
                lib::debug("tty: opened ({}, {}) for pid {}", major(rdev), minor(rdev), self->pid);
            return true;
        }

        bool close(std::shared_ptr<vfs::file> self) override
        {
            lib::bug_on(!self || !self->private_data);
            const auto inst = std::static_pointer_cast<instance>(self->private_data);

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
                        return true;

                    if (!generic_close(self, inst))
                    {
                        // can't even close ttys properly smh
                        inst->ref.fetch_add(1, std::memory_order_relaxed);
                        return false;
                    }
                    lib::bug_on(!locked->erase(inst->minor));
                }
                drv->destroy_instance(inst);
            }
            self->private_data.reset();

            if constexpr (debug)
            {
                const auto rdev = self->path.dentry->inode->stat.st_rdev;
                lib::debug("tty: closed ({}, {}) for pid {}", major(rdev), minor(rdev), self->pid);
            }
            return true;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->read(file, buffer);
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->write(file, buffer);
        }

        int ioctl(std::shared_ptr<vfs::file> file, unsigned long request, lib::uptr_or_addr argp) override
        {
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->ioctl(request, argp);
        }

        bool trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return true;
        }
    };

    struct current_ops : vfs::ops
    {
        static std::shared_ptr<current_ops> singleton()
        {
            static auto instance = std::make_shared<current_ops>();
            return instance;
        }

        bool open(std::shared_ptr<vfs::file> self, int flags) override
        {
            lib::bug_on(!self || self->private_data != nullptr);
            lib::unused(flags);

            const auto proc = sched::proc_for(self->pid);
            lib::bug_on(!proc);

            auto sess = sched::session_for(proc->sid);
            lib::bug_on(!sess);

            auto ctty = sess->controlling_tty.lock();
            if (!ctty.value())
                return (errno = ENXIO, false);

            // it's already open so just increment the ref count
            ctty.value()->ref.fetch_add(1, std::memory_order_acq_rel);
            self->private_data = ctty.value();

            if constexpr (debug)
                lib::debug("tty: opened (5, 0) for pid {}", self->pid);
            return true;
        }

        bool close(std::shared_ptr<vfs::file> self) override
        {
            return tty::ops::singleton()->close(self);
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->read(file, buffer);
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->write(file, buffer);
        }

        int ioctl(std::shared_ptr<vfs::file> file, unsigned long request, lib::uptr_or_addr argp) override
        {
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->ioctl(request, argp);
        }

        bool trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return true;
        }
    };

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.dev.tty-registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task tty_task
    {
        "vfs.dev.tty.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { devtmpfs::mounted_stage() },
        lib::initgraph::entail { registered_stage() },
        [] {
            register_cdev(current_ops::singleton(), makedev(5, 0));
            if (const auto ret = vfs::create(std::nullopt, "/dev/tty", stat::s_ifchr | 0666, makedev(5, 0)); !ret)
            {
                lib::panic(
                    "tty: could not create /dev/tty: {}",
                    magic_enum::enum_name(ret.error())
                );
            }

            const auto test_drv = new test_driver { };
            drivers.push_back(test_drv);

            const auto add_test_tty = [&]
            {
                static std::size_t minor = 0;
                register_cdev(ops::singleton(), makedev(test_drv->major, minor));

                const auto name = fmt::format("/dev/tty{}", minor);
                auto ret = vfs::create(
                    std::nullopt, name, stat::s_ifchr | 0666,
                    makedev(test_drv->major, minor)
                );

                minor++;
                if (!ret.has_value())
                {
                    lib::error(
                        "tty: could not create '{}': {}",
                        name, magic_enum::enum_name(ret.error())
                    );
                }
            };

            for (std::size_t i = 0; i < 4; i++)
                add_test_tty();
        }
    };
} // namespace fs::dev::tty