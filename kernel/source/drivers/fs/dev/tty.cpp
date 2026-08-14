// Copyright (C) 2024-2026  ilobilo

module drivers.fs.dev.tty;

import drivers.output.terminal;
import drivers.fs.devtmpfs;
import system.memory.virt;
import system.vfs.dev;
import system.dev;
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
        lib::locker<console_target_t, sched::mutex_t> console_target;

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
            std::shared_ptr<vfs::file_t> file, std::shared_ptr<instance> inst,
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

                        locked->set_group(proc->group);
                        locked->session = proc->session;
                    }
                }
            }

            return { };
        }

        lib::expect<void> generic_close(vfs::file_t &file, std::shared_ptr<instance> inst)
        {
            lib::unused(file);
            return inst->close();
        }

        lib::expect<std::shared_ptr<instance>> open_or_create(
            std::shared_ptr<vfs::file_t> file, driver *drv, std::uint32_t min,
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

        struct alias_ops : vfs::ops_t
        {
            bool seekable() const override { return false; }

            lib::expect<void> close(vfs::file_t &file) override
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

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);
                lib::bug_on(!file || !file->private_data);
                const auto inst = std::static_pointer_cast<instance>(file->private_data);
                return inst->read(std::move(file), buffer);
            }

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);
                lib::bug_on(!file || !file->private_data);
                const auto inst = std::static_pointer_cast<instance>(file->private_data);
                return inst->write(std::move(file), buffer);
            }

            lib::expect<int> ioctl(
                std::shared_ptr<vfs::file_t> file, std::uint64_t request,
                lib::uptr_or_addr argp
            ) override
            {
                lib::bug_on(!file || !file->private_data);
                const auto inst = std::static_pointer_cast<instance>(file->private_data);
                return inst->ioctl(request, argp);
            }

            lib::expect<std::uint16_t> poll(
                std::shared_ptr<vfs::file_t> file, vfs::poll_table_t *pt
            ) override
            {
                lib::bug_on(!file || !file->private_data);
                const auto inst = std::static_pointer_cast<instance>(file->private_data);
                return inst->poll(pt);
            }
        };

        lib::expect<void> background_check(instance &inst, int sig)
        {
            const auto thread = sched::current_thread();
            const auto proc = thread->proc.get();

            auto fg_group = inst.ctrl.lock()->group.lock();
            if (!fg_group || fg_group == proc->group)
                return { };

            if (thread->sigmask.has(sig))
                return { };

            const bool sig_ign = [&] {
                auto sa = proc->sigactions;
                if (!sa)
                    return false;

                const std::unique_lock _ { sa->lock };
                return sa->actions[sig - 1].handler == sched::sig_ign;
            } ();

            if (sig_ign)
                return { };

            const bool is_orphaned = [&] {
                for (const auto &[_, weak] : *proc->group->members.lock())
                {
                    auto member = weak.lock();
                    if (!member)
                        continue;

                    auto parent = member->parent.lock();
                    if (!parent)
                        continue;

                    if (parent->session == proc->group->session && parent->group != proc->group)
                        return false;
                }
                return true;
            } ();

            if (is_orphaned)
                return std::unexpected { lib::err::io_error };

            proc->group->signal_all(sig);
            sched::consume_pending_stops();
            return { };
        }

        std::string device_name(const driver &drv, std::size_t idx)
        {
            if (drv.typ == type::pty)
            {
                static const char ptychar[] = "pqrstuvwxyzabcde";
                const auto i = idx + drv.name_base;
                return fmt::format("{}{}{}",
                    drv.subtyp == subtype::pty_slave ? "tty" : drv.name,
                    ptychar[i >> 4 & 0xF], i & 0xF
                );
            }
            if (drv.flags & unnumbered)
                return drv.name;
            return fmt::format("{}{}", drv.name, idx + drv.name_base);
        }

        struct tty_class_t final : ::dev::class_t
        {
            tty_class_t() : ::dev::class_t { "tty", ::dev::empty_ktype(), false } { }

            std::string devnode(const ::dev::device_t &device, mode_t &mode) const override
            {
                mode = (mode & ~static_cast<mode_t>(0777)) | 0666;
                return device.name;
            }
        };

        struct tty_ktype_t : ::dev::ktype_t
        {
            std::span<::dev::attribute_t *const> attributes() const override
            {
                static ::dev::attribute_t *list[] {
                    ::dev::dev_attribute()
                };
                return list;
            }
        };

        struct redirect_t
        {
            driver *drv;
            redirect_fn resolve;
        };

        struct redirect_ktype_t final : tty_ktype_t
        {
            std::span<::dev::attribute_t *const> attributes() const override
            {
                static ::dev::make_attribute_t active {
                    [](::dev::device_t &device) -> lib::expect<std::string> {
                        const auto red = std::static_pointer_cast<redirect_t>(device.private_data);
                        if (!red || !red->drv || !red->resolve)
                            return std::unexpected { lib::err::io_error };

                        const auto minor = red->resolve();
                        if (minor < red->drv->minor_start)
                            return std::unexpected { lib::err::io_error };

                        return device_name(*red->drv, minor - red->drv->minor_start) + '\n';
                    }, nullptr, "active", 0444
                };

                static ::dev::attribute_t *list[] {
                    &active,
                    ::dev::dev_attribute()
                };
                return list;
            }
        };

        std::shared_ptr<::dev::kobject_t> tty_parent()
        {
            static const auto parent = [] {
                lib::panic_if(
                    !::dev::register_class(get_class()),
                    "tty: could not register class 'tty'"
                );

                auto kobj = ::dev::kobject_t::create(
                    "tty", ::dev::empty_ktype(), ::dev::root("/devices/virtual")
                );
                lib::panic_if(
                    !::dev::register_kobject(kobj),
                    "tty: could not register '/devices/virtual/tty'"
                );
                return kobj;
            } ();
            return parent;
        }

        void add_device(
            std::string name, dev_t rdev, std::shared_ptr<vfs::ops_t> fops,
            std::shared_ptr<redirect_t> redirect = nullptr
        )
        {
            static tty_ktype_t def { };
            static redirect_ktype_t redir { };

            auto &type = redirect
                ? static_cast<::dev::ktype_t &>(redir)
                : static_cast<::dev::ktype_t &>(def);

            auto device = ::dev::device_t::create(name, type, tty_parent());
            device->cls = &get_class();
            device->devt = rdev;
            device->fops = std::move(fops);
            device->private_data = std::move(redirect);

            if (const auto ret = ::dev::register_device(std::move(device)); !ret)
            {
                lib::error(
                    "tty: could not register '{}': {}",
                    name, lib::error_name(ret.error())
                );
            }
        }
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
                locked->reset_group();
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
        out_buffer { }, output_lock { }, out_wq { }, stopped { false },
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

    void default_ldisc::wait_sent()
    {
        while (!inst->hung_up.load(std::memory_order_relaxed))
        {
            const auto gen = out_wq.snapshot_gen();
            {
                const std::unique_lock _ { output_lock };
                if (out_buffer.empty())
                    return;
            }
            out_wq.wait_prepared(gen);
        }
    }

    void default_ldisc::write_wake()
    {
        output_flush();
        out_wq.wake_all();
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

    bool default_ldisc::maybe_readable()
    {
        lib::bug_on(!inst);

        if (inst->hung_up.load(std::memory_order_relaxed))
            return true;

        if (!raw_buffer.empty() || !inst->raw_buffer.empty())
            return true;

        auto in_locked = in_buffer.lock();
        return in_locked->read_head != in_locked->read_tail;
    }

    void default_ldisc::set_stopped(bool value)
    {
        lib::bug_on(!inst);

        bool changed;
        {
            const std::unique_lock _ { output_lock };
            changed = stopped.exchange(value, std::memory_order_relaxed) != value;
        }
        if (changed)
            inst->flow_notify(value);
    }

    void default_ldisc::output_flush()
    {
        const std::unique_lock _ { output_lock };
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
            if (const auto res = inst->transmit(std::move(span)); res != num_chars)
                lib::error("tty: could not transmit {} characters (got {})", num_chars, res);
        }
    }

    void default_ldisc::output_clear()
    {
        const std::unique_lock _ { output_lock };
        out_buffer.clear();
        out_wq.wake_all();
    }

    void default_ldisc::input_flush_locked()
    {
        inst->raw_buffer.clear();
        raw_buffer.clear();

        {
            auto in_locked = in_buffer.lock();
            in_locked->read_tail = in_locked->read_head;
            in_locked->cooked_head = in_locked->read_head;
        }

        inst->wakeup_link();
    }

    void default_ldisc::input_flush()
    {
        const auto _ = inst->termios.lock();
        input_flush_locked();
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

        bool wake_readers = false;
        const auto flush_wakeup = [&] {
            if (!std::exchange(wake_readers, false))
                return;
            self->in_wq.wake_all();
        };

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

                flush_wakeup();
                self->should_work.store(true, std::memory_order_relaxed);
                sched::thread_exit(0);
            }

            const auto raw_gen = self->raw_wq.snapshot_gen();
            const auto hung_gen = self->hung_wq.snapshot_gen();

            auto tios = self->inst->termios.lock();
            auto ret = self->raw_buffer.pop();

            if (!ret.has_value())
            {
                tios.unlock();
                flush_wakeup();
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
                    self->hung_wq.wait_prepared(hung_gen);
                }
                else self->raw_wq.wait_prepared(raw_gen);
                continue;
            }
            auto chr = static_cast<char>(ret.value());

            termios = *tios;
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
                        self->input_flush_locked();
                        self->output_clear();
                        self->inst->flush_notify(tcioflush);
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
                    self->set_stopped(true);
                    continue;
                }
                else if (chr == termios.c_cc[vstart])
                {
                    self->set_stopped(false);
                    continue;
                }
            }

            if ((termios.c_iflag & ixany) && self->stopped.load(std::memory_order_relaxed))
                self->set_stopped(false);

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
                        wake_readers = true;
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
                        wake_readers = true;
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
                    wake_readers = true;

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

    lib::expect<std::size_t> default_ldisc::read(std::shared_ptr<vfs::file_t> file, lib::maybe_uspan<std::byte> buffer)
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

                    const auto gen = in_wq.snapshot_gen();
                    in_locked.unlock();
                    in_wq.wait_prepared(gen);
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

                    const auto gen = in_wq.snapshot_gen();
                    in_locked.unlock();
                    in_wq.wait_prepared(gen);
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

                    const auto gen = in_wq.snapshot_gen();
                    in_locked.unlock();
                    in_wq.wait_prepared(gen, ms * 1'000'000);
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

                    const auto gen = in_wq.snapshot_gen();
                    in_locked.unlock();
                    in_wq.wait_prepared(gen);
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

                        const auto gen = in_wq.snapshot_gen();
                        in_locked.unlock();
                        const auto res = in_wq.wait_prepared(gen, ms * 1'000'000);
                        in_locked.lock();

                        available = get_available(in_locked);
                        if (!res.interrupted && !res.killed && available == 0)
                        {
                            lib::bug_on(!res.expired);
                            return progress;
                        }
                    }
                }
                return progress;
            }
        }
        std::unreachable();
    }

    lib::expect<std::size_t> default_ldisc::write(std::shared_ptr<vfs::file_t> file, lib::maybe_uspan<std::byte> buffer)
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

                const auto gen = out_wq.snapshot_gen();
                if (output_append(termios, static_cast<char>(buf.data()[i])))
                {
                    progress++;
                    continue;
                }
                out_wq.wait_prepared(gen);
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
            case tcxonc:
            {
                const auto termios = inst->termios.lock().value();
                switch (argp.value())
                {
                    case tcooff:
                        set_stopped(true);
                        break;
                    case tcoon:
                        set_stopped(false);
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
                    wait_sent();

                auto wlocked = inst->termios.lock();
                if (request == tcsetsf)
                {
                    input_flush_locked();
                    inst->flush_notify(tciflush);
                }

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
                    wait_sent();

                auto wlocked = inst->termios.lock();
                if (request == tcsetsf2)
                {
                    input_flush_locked();
                    inst->flush_notify(tciflush);
                }

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
                switch (argp.value())
                {
                    case tciflush:
                        input_flush();
                        break;
                    case tcoflush:
                        output_clear();
                        break;
                    case tcioflush:
                        input_flush();
                        output_clear();
                        break;
                    default:
                        return std::unexpected { lib::err::invalid_flags };
                }
                inst->flush_notify(argp.value());
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
            case tiocinq:
            {
                const auto tios = inst->termios.lock();
                const auto in_locked = in_buffer.lock();
                const int count = (tios->c_lflag & ktermios::lflag::icanon)
                    ? in_locked->cooked_head - in_locked->read_tail
                    : in_locked->read_head - in_locked->read_tail;

                if (!argp.write(count))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case tiocoutq:
            {
                const std::unique_lock _ { output_lock };
                const int value = out_buffer.size();
                if (!argp.write(value))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            default:
                lib::error("tty: unhandled ioctl: 0x{:X}", request);
                break;
        }
        return std::unexpected { lib::err::inappropriate_ioctl };
    }

    lib::expect<std::uint16_t> default_ldisc::poll(vfs::poll_table_t *pt)
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

        std::size_t available = 0;
        {
            const auto tios = inst->termios.lock();
            auto in_locked = in_buffer.lock();
            available = (tios->c_lflag & ktermios::lflag::icanon)
                ? (in_locked->cooked_head - in_locked->read_tail)
                : (in_locked->read_head - in_locked->read_tail);
        }

        if (hung_up || available > 0 || !raw_buffer.empty() || !inst->raw_buffer.empty())
            mask |= pollin;

        if (!hung_up)
        {
            const std::unique_lock _ { output_lock };
            if (!out_buffer.full())
                mask |= pollout;
        }

        if (hung_up)
            mask |= pollhup | pollerr;

        return mask;
    }

    instance::instance(driver *drv, std::uint32_t minor, std::shared_ptr<line_discipline> ld)
        : drv { drv }, minor { minor }, ref { 0 }, hung_up { false }, ldisc { ld },
          termios { drv->init_termios }, termios_locked { ktermios { } },
          winsize { winsize::standard() }, ctrl { }, raw_buffer { }, raw_wq { },
          worker_thread { }, raw_should_work { ld != nullptr }
    {
        lib::bug_on(drv == nullptr);
        if (ld)
            worker_thread = sched::spawn(raw_worker, this, -10);
    }

    instance::~instance()
    {
        if (!raw_should_work.load(std::memory_order_acquire))
        {
            lib::bug_on(!worker_thread.expired());
            return;
        }

        raw_should_work.store(false, std::memory_order_release);
        raw_wq.wake_one();

        while (!raw_should_work.load(std::memory_order_acquire))
            sched::yield();
    }

    [[noreturn]]
    void instance::raw_worker(instance *self)
    {
        lib::bug_on(!self);

        while (true)
        {
            const auto gen = self->raw_wq.snapshot_gen();
            if (!self->raw_should_work.load(std::memory_order_acquire))
            {
                self->raw_should_work.store(true, std::memory_order_release);
                sched::thread_exit(0);
            }

            std::array<std::byte, 64> chunk;

            auto tios = self->termios.lock();
            const auto num = self->raw_buffer.pop(std::span { chunk });

            if (num == 0)
            {
                tios.unlock();
                self->raw_wq.wait_prepared(gen);
                continue;
            }

            self->wakeup_link();

            auto ld = self->ldisc.lock().value();
            lib::bug_on(!ld);
            ld->receive(std::span { chunk.data(), num });
        }
    }

    std::shared_ptr<instance> instance::real_tty()
    {
        auto self = shared_from_this();
        if (drv->subtyp != subtype::pty_master)
            return self;
        if (auto peer = link.lock())
            return peer;
        return self;
    }

    void instance::resize(const struct winsize &size)
    {
        {
            auto current = winsize.lock();
            if (!std::memcmp(&*current, &size, sizeof(size)))
                return;
            *current = size;
        }

        const auto fg_group = ctrl.lock()->group.lock();
        if (fg_group)
            fg_group->signal_all(sched::sigwinch);
    }

    lib::expect<int> instance::ioctl(std::uint64_t request, lib::uptr_or_addr argp)
    {
        lib::bug_on(!drv);

        const auto target = real_tty();
        switch (request)
        {
            case tcsbrk:
            {
                auto ld = ldisc.lock().value();
                if (!ld)
                    return std::unexpected { lib::err::io_error };
                ld->wait_sent();
                if (argp.value() == 0)
                {
                    break_ctl(true);
                    sched::sleep_for_ns(250'000'000);
                    break_ctl(false);
                }
                return 0;
            }
            case tiocgpgrp:
            {
                const auto proc = sched::current_process();

                auto locked = target->ctrl.lock();
                if (locked->session.lock() != proc->session)
                    return std::unexpected { lib::err::inappropriate_ioctl };

                if (locked->pgid == 0)
                    return std::unexpected { lib::err::inappropriate_ioctl };
                if (!argp.write(locked->pgid))
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

                std::shared_ptr<sched::session_t> tty_session;
                {
                    auto locked = target->ctrl.lock();
                    tty_session = locked->session.lock();
                    if (!tty_session || tty_session != proc->session)
                        return std::unexpected { lib::err::inappropriate_ioctl };
                }

                if (const auto ret = background_check(*target, sched::sigttou); !ret)
                    return std::unexpected { ret.error() };

                std::shared_ptr<sched::group_t> new_group;
                {
                    auto members = tty_session->members.lock();
                    auto it = members->find(pgid);
                    if (it == members->end())
                        return std::unexpected { lib::err::not_permitted };
                    new_group = it->second.lock();
                    if (!new_group)
                        return std::unexpected { lib::err::not_permitted };
                }
                target->ctrl.lock()->set_group(std::move(new_group));
                return 0;
            }
            case tiocgsid:
            {
                const auto proc = sched::current_process();

                auto locked = target->ctrl.lock();
                const auto tty_session = locked->session.lock();
                if (!tty_session || tty_session != proc->session)
                    return std::unexpected { lib::err::inappropriate_ioctl };

                if (!argp.write(tty_session->sid))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case tiocgwinsz:
                if (!argp.write(target->winsize.lock().value()))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            case tiocswinsz:
            {
                struct winsize size;
                if (!argp.read(size))
                    return std::unexpected { lib::err::invalid_address };
                target->resize(size);
                return 0;
            }
            case tiocsctty:
            {
                const int force = argp.value();

                const auto proc = sched::current_process();
                if (proc->pid != proc->session->sid)
                    return std::unexpected { lib::err::not_permitted };

                auto locked = target->ctrl.lock();
                if (auto existing = locked->session.lock())
                {
                    if (existing == proc->session)
                        return 0;

                    if (force != 1 || !sched::capable(sched::cap_t::sys_admin))
                        return std::unexpected { lib::err::not_permitted };

                    auto old_ctty = existing->ctty.lock();
                    if (old_ctty.value().get() == target.get())
                        old_ctty.value().reset();

                    locked->session.reset();
                    locked->reset_group();
                }

                {
                    auto locked = proc->session->ctty.lock();
                    if (locked.value())
                        return std::unexpected { lib::err::not_permitted };
                    locked.value() = target;
                }

                locked->session = proc->session;
                locked->set_group(proc->group);
                return 0;
            }
            case tiocnotty:
            {
                const auto proc = sched::current_process();
                std::shared_ptr<sched::group_t> fg_group;
                {
                    auto locked = ctrl.lock();
                    if (locked->session.lock() != proc->session)
                        return std::unexpected { lib::err::inappropriate_ioctl };

                    if (proc->pid != proc->session->sid)
                        return 0;

                    fg_group = locked->group.lock();
                    locked->session.reset();
                    locked->reset_group();
                }

                {
                    auto ctty_locked = proc->session->ctty.lock();
                    if (ctty_locked.value().get() == this)
                        ctty_locked.value().reset();
                }

                if (fg_group)
                {
                    fg_group->signal_all(sched::sighup);
                    fg_group->signal_all(sched::sigcont);
                }
                return 0;
            }
        }

        const auto res = drv->ioctl(this, request, argp);
        if (res.has_value() || (res.error() != lib::err::inappropriate_ioctl))
            return res;

        auto ldisc_target = shared_from_this();
        switch (request)
        {
            case tcgets:
            case tcsets:
            case tcsetsw:
            case tcsetsf:
            case tcgets2:
            case tcsets2:
            case tcsetsw2:
            case tcsetsf2:
            case tiocglcktrmios:
            case tiocslcktrmios:
                ldisc_target = target;
                break;
        }

        auto ld = ldisc_target->ldisc.lock().value();
        if (!ld)
            return std::unexpected { lib::err::io_error };
        return ld->ioctl(request, argp);
    }

    lib::expect<std::uint16_t> instance::poll(vfs::poll_table_t *pt)
    {
        auto ld = ldisc.lock().value();
        if (!ld)
            return std::unexpected { lib::err::io_error };
        return ld->poll(pt);
    }

    void instance::wakeup_link()
    {
        const auto peer = link.lock();
        if (!peer)
            return;

        const auto peer_ld = peer->ldisc.lock().value();
        if (peer_ld)
            peer_ld->write_wake();
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
            locked->reset_group();
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

        lib::expect<void> open(std::shared_ptr<vfs::file_t> file, int flags, pid_t pid) override
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

        lib::expect<void> open(std::shared_ptr<vfs::file_t> file, int flags, pid_t pid) override
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

        lib::expect<void> open(std::shared_ptr<vfs::file_t> file, int flags, pid_t pid) override
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

    struct redirect_ops : alias_ops
    {
        driver *drv;
        redirect_fn resolve;

        lib::expect<void> open(std::shared_ptr<vfs::file_t> file, int flags, pid_t pid) override
        {
            lib::bug_on(!file || file->private_data != nullptr);
            lib::bug_on(!drv || !resolve);

            auto res = open_or_create(file, drv, resolve(), flags, pid);
            if (!res)
                return std::unexpected { res.error() };
            file->private_data = std::move(*res);
            return { };
        }

        redirect_ops(driver *drv, redirect_fn resolve)
            : drv { drv }, resolve { resolve } { }
    };

    void register_redirect(dev_t rdev, driver *drv, redirect_fn fn)
    {
        lib::bug_on(!drv || !fn);

        add_device(
            fmt::format("{}{}", drv->name, minor(rdev)), rdev,
            std::make_shared<redirect_ops>(drv, fn),
            std::make_shared<redirect_t>(drv, fn)
        );
    }

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
        register_ops(rdev, ops::singleton());
    }

    void register_driver(driver *drv)
    {
        lib::bug_on(!drv);
        lib::debug("tty: registering driver '{}'", drv->driver_name);
        drivers.push_back(drv);

        if (drv->flags & dynamic)
            return;

        const auto add_one = [&](std::size_t idx, std::uint32_t minor) {
            const auto name = device_name(*drv, idx);
            add_device(name, makedev(drv->major, minor), ops::singleton());

            if constexpr (debug)
                lib::debug("tty: created ({}, {}) as '{}'", drv->major, minor, name);
        };

        lib::panic_if((drv->flags & unnumbered) && drv->num_devices > 1);
        for (std::size_t i = 0; i < drv->num_devices; i++)
            add_one(i, drv->minor_start + i);
    }

    void register_device(std::string_view name, dev_t rdev, std::shared_ptr<vfs::ops_t> fops)
    {
        add_device(std::string { name }, rdev, std::move(fops));
    }

    ::dev::class_t &get_class()
    {
        static tty_class_t cls { };
        return cls;
    }

    lib::initgraph::task tty_task
    {
        "vfs.dev.tty.current.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            devtmpfs::mounted_stage(),
            ::dev::available_stage()
        },
        [] {
            add_device("tty", makedev(5, 0), current_ops::singleton());
            add_device("console", makedev(5, 1), console_ops::singleton());
        }
    };
} // namespace fs::dev::tty
