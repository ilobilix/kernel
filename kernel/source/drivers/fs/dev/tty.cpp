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

            inst->flush_buffer();

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

    default_ldisc::default_ldisc(instance *inst) : line_discipline { inst }
    {
        sched::spawn(0, reinterpret_cast<std::uintptr_t>(worker), reinterpret_cast<std::uintptr_t>(this), -10);
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

        const auto is_control = [](char chr)
        {
            return chr < ' ' || chr == 0x7F;
        };

        ktermios termios;
        const auto push_to_echo = [&](char chr)
        {
            if (!(termios.c_oflag & opost))
            {
                self->echo_buffer.push(chr);
                self->echo_sem.signal(true);
                return;
            }

            if (chr == '\n' && (termios.c_oflag & onlcr))
            {
                self->echo_buffer.push('\r');
                self->echo_buffer.push('\n');
                self->echo_sem.signal(true);
                return;
            }

            if (chr == '\r' && (termios.c_oflag & onlret))
                return;

            if (termios.c_oflag & olcuc)
                chr = std::toupper(chr);

            self->echo_buffer.push(chr);
            self->echo_sem.signal(true);
        };

        const auto move_cooked = [&](auto &wlocked)
        {
            for (std::size_t i = 0; i < wlocked->size; i++)
            {
                self->in_buffer.push(wlocked->buffer[i]);
                self->in_sem.signal(true);
            }
            wlocked->size = 0;
        };

        const auto erase_cooked = [&](auto &wlocked, bool erase)
        {
            if (wlocked->size > 0)
            {
                auto vwidth = 1;
                wlocked->size--;
                if (is_control(wlocked->buffer[wlocked->size]))
                    vwidth = 2;
                wlocked->buffer[wlocked->size] = 0;
                if (termios.c_lflag & echo)
                {
                    if (erase)
                    {
                        for (auto i = 0; i < vwidth; i++)
                        {
                            push_to_echo('\b');
                            push_to_echo(' ');
                            push_to_echo('\b');
                        }
                    }
                }
            }
        };

        bool next_is_verbatim = false;
        while (true)
        {
            auto ret = self->raw_buffer.pop();
            if (!ret.has_value())
            {
                self->raw_sem.wait();
                continue;
            }
            auto chr = static_cast<char>(ret.value());

            termios = self->inst->termios.read_lock().value();
            if (next_is_verbatim)
            {
                next_is_verbatim = false;
                auto wlocked = self->cooked_buffer.write_lock();
                if (wlocked->size < cooked_t::cap)
                {
                    wlocked->buffer[wlocked->size] = chr;
                    wlocked->size++;
                }

                if (termios.c_lflag & echo)
                {
                    if ((termios.c_lflag & echoctl) && is_control(chr))
                    {
                        push_to_echo('^');
                        push_to_echo((chr + '@') % 128);
                    }
                    else push_to_echo(chr);
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
                    push_to_echo('^');
                    push_to_echo('\b');
                    continue;
                }

                if (chr == termios.c_cc[vkill])
                {
                    {
                        auto wlocked = self->cooked_buffer.write_lock();
                        while (wlocked->size > 0)
                            erase_cooked(wlocked, termios.c_lflag & echok);
                    }

                    if ((termios.c_lflag & echo) && !(termios.c_lflag & echok))
                    {
                        if ((termios.c_lflag & echoctl) && is_control(chr))
                        {
                            push_to_echo('^');
                            push_to_echo((chr + '@') % 128);
                        }
                        else push_to_echo(chr);
                    }
                    continue;
                }

                if (chr == termios.c_cc[verase])
                {
                    {
                        auto wlocked = self->cooked_buffer.write_lock();
                        erase_cooked(wlocked, termios.c_lflag & echoe);
                    }

                    if ((termios.c_lflag & echo) && !(termios.c_lflag & echoe))
                    {
                        if ((termios.c_lflag & echoctl) && is_control(chr))
                        {
                            push_to_echo('^');
                            push_to_echo((chr + '@') % 128);
                        }
                        else push_to_echo(chr);
                    }
                    continue;
                }

                if (chr == termios.c_cc[vwerase] && (termios.c_lflag & iexten))
                {
                    {
                        auto wlocked = self->cooked_buffer.write_lock();
                        while (wlocked->size > 0 && wlocked->buffer[wlocked->size - 1] == ' ')
                            erase_cooked(wlocked, termios.c_lflag & echoe);
                        while (wlocked->size > 0 && wlocked->buffer[wlocked->size - 1] != ' ')
                            erase_cooked(wlocked, termios.c_lflag & echoe);
                    }

                    if ((termios.c_lflag & echo) && !(termios.c_lflag & echoe))
                    {
                        if ((termios.c_lflag & echoctl) && is_control(chr))
                        {
                            push_to_echo('^');
                            push_to_echo((chr + '@') % 128);
                        }
                        else push_to_echo(chr);
                    }
                    continue;
                }

                {
                    auto wlocked = self->cooked_buffer.write_lock();
                    if (chr == termios.c_cc[veof])
                    {
                        move_cooked(wlocked);
                        continue;
                    }

                    if (wlocked->size < cooked_t::cap)
                    {
                        wlocked->buffer[wlocked->size] = chr;
                        wlocked->size++;
                    }
                }

                if (termios.c_lflag & echo)
                {
                    if ((termios.c_lflag & echoctl) && is_control(chr) && chr != '\n')
                    {
                        push_to_echo('^');
                        push_to_echo((chr + '@') % 128);
                    }
                    else push_to_echo(chr);
                }

                if (chr == '\n' || (termios.c_cc[veol] && chr == termios.c_cc[veol]))
                {
                    if (!(termios.c_lflag & echo) && (termios.c_lflag & echonl))
                        push_to_echo(chr);
                    auto wlocked = self->cooked_buffer.write_lock();
                    wlocked->buffer[wlocked->size - 1] = chr;
                    move_cooked(wlocked);
                    continue;
                }
                continue;
            }
            else if (termios.c_lflag & echo)
            {
                if ((termios.c_lflag & echoctl) && is_control(chr) && chr != '\n')
                {
                    push_to_echo('^');
                    push_to_echo((chr + '@') % 128);
                }
                else push_to_echo(chr);
            }

            self->in_buffer.push(chr);
            self->in_sem.signal(true);
        }
    }

    std::ssize_t default_ldisc::read(lib::maybe_uspan<std::byte> buffer)
    {
        // TODO
        lib::unused(buffer);
        return -1;
    }

    std::ssize_t default_ldisc::write(lib::maybe_uspan<std::byte> buffer)
    {
        // TODO
        lib::bug_on(!inst);
        return inst->transmit(buffer);
    }

    instance::instance(driver *drv, std::uint32_t minor, std::unique_ptr<line_discipline> ldisc)
        : drv { drv }, minor { minor }, ref { 0 }, ldisc { std::move(ldisc) },
          termios { drv->init_termios }, winsize { }, ctrl { }
    { lib::bug_on(drv == nullptr); }

    struct test_driver : driver
    {
        struct test_instance : instance
        {
            bool open(std::shared_ptr<vfs::file> self) override
            {
                lib::unused(self);
                return true;
            }

            bool close() override { return true; }

            std::ssize_t transmit(lib::maybe_uspan<std::byte> buffer) override
            {
                lib::membuffer buf { buffer.size_bytes() };
                buffer.copy_to(buf.span());
                const std::string_view str { reinterpret_cast<const char *>(buf.data()), buf.size_bytes() };
                lib::print("{}", str);
                return buffer.size_bytes();
            }

            void flush_buffer() override { }

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
            return inst->read(buffer);
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->write(buffer);
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
            return inst->read(buffer);
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->write(buffer);
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
            auto ret = vfs::create(std::nullopt, "/dev/tty", stat::s_ifchr | 0666, makedev(5, 0));
            lib::panic_if(!ret.has_value(), "tty: could not create /dev/tty: {}", magic_enum::enum_name(ret.error()));

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