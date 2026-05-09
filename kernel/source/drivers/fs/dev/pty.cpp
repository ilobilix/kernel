// Copyright (C) 2024-2026  ilobilo

module drivers.fs.dev.pty;

import drivers.fs.devtmpfs;
import drivers.fs.devpts;
import drivers.fs.dev.tty;
import system.sched.mutex;
import system.sched;
import system.vfs.dev;
import fmt;

namespace fs::dev::pty
{
    namespace
    {
        constexpr std::size_t master_count = 8;
        constexpr std::uint32_t master_major = 128;
        constexpr std::uint32_t slave_major = master_major + master_count;

        constexpr std::uint64_t tiocgptn = 0x80045430;
        constexpr std::uint64_t tiocsptlck = 0x40045431;
        constexpr std::uint64_t tiocgptlck = 0x80045439;
        constexpr std::uint64_t tiocgptpeer = 0x5441;

        constexpr mode_t slave_mode = stat::s_ifchr | 0620;
        constexpr tty::ktermios slave_termios = tty::ktermios::standard();
        constexpr tty::ktermios master_termios = []
        {
            auto tios = tty::ktermios::standard();
            tios.c_iflag = 0;
            tios.c_oflag = 0;
            tios.c_lflag = 0;
            return tios;
        }();
    } // namespace

    struct ptm_instance;
    struct pts_instance;
    struct ptm_driver;
    struct pts_driver;

    struct pair
    {
        std::uint32_t minor;
        std::shared_ptr<ptm_instance> master;
        std::shared_ptr<pts_instance> slave;
        std::atomic_bool locked { true };
    };

    namespace
    {
        ptm_driver *ptm = nullptr;
        pts_driver *pts = nullptr;

        struct allocator_t
        {
            std::array<bool, master_count> minor_used { };
            lib::map::flat_hash<std::uint32_t, std::shared_ptr<pair>> pairs;
        };
        lib::locker<allocator_t, sched::mutex> allocator;

        std::shared_ptr<pair> find_pair(std::uint32_t pty_minor)
        {
            auto state = allocator.lock();
            if (auto iter = state->pairs.find(pty_minor); iter != state->pairs.end())
                return iter->second;
            return nullptr;
        }

        void hangup_peer(tty::instance &self)
        {
            if (auto peer = self.link.lock())
                peer->hangup();
        }

        void propagate_winsize(tty::instance &from, tty::instance &to)
        {
            const auto current = from.winsize.lock().value();
            to.winsize.lock().value() = current;
        }

        void signal_winch(tty::instance &slave_inst)
        {
            std::shared_ptr<sched::group_t> fg_group;
            {
                auto locked = slave_inst.ctrl.lock();
                fg_group = locked->group.lock();
            }
            if (fg_group)
                fg_group->signal_all(sched::sigwinch);
        }
    } // namespace

    struct pty_instance_base : tty::instance
    {
        pty_instance_base(tty::driver *drv, std::uint32_t pty_minor)
            : instance { drv, pty_minor, std::make_shared<tty::default_ldisc>(this) } { }

        std::size_t transmit(std::span<std::byte> buffer) override
        {
            auto peer = link.lock();
            if (!peer || !peer->receive(buffer))
                return 0;
            return buffer.size_bytes();
        }

        std::size_t can_transmit() override
        {
            auto peer = link.lock();
            return peer ? peer->raw_buffer.available() : 0;
        }

        lib::expect<void> open(std::shared_ptr<vfs::file>) override { return { }; }

        lib::expect<void> close() override
        {
            hangup_peer(*this);
            return { };
        }

        bool needs_close_erase() const override
        {
            return find_pair(minor) == nullptr;
        }
    };

    struct ptm_instance : pty_instance_base
    {
        using pty_instance_base::pty_instance_base;

        lib::expect<int> ioctl(std::uint64_t request, lib::uptr_or_addr argp) override;
    };

    struct pts_instance : pty_instance_base
    {
        using pty_instance_base::pty_instance_base;

        lib::expect<void> permit_open(std::shared_ptr<vfs::file>) override;
        lib::expect<int> ioctl(std::uint64_t request, lib::uptr_or_addr argp) override;
    };

    struct pty_driver_base : tty::driver
    {
        using tty::driver::driver;

        // pty instances are allocated by alloc()
        std::shared_ptr<tty::instance> create_instance(std::uint32_t) override { return nullptr; }
        void destroy_instance(std::shared_ptr<tty::instance>) override { }
    };

    struct ptm_driver : pty_driver_base
    {
        ptm_driver() : pty_driver_base {
            "pty_master", "ptm", 0, master_major, 0, master_count,
            tty::flag::dynamic, tty::type::pty, tty::subtype::pty_master,
            master_termios
        } { }
    };

    struct pts_driver : pty_driver_base
    {
        pts_driver() : pty_driver_base {
            "pty_slave", "pts", 0, slave_major, 0, master_count,
            tty::flag::dynamic, tty::type::pty, tty::subtype::pty_slave,
            slave_termios
        } { }
    };

    lib::expect<void> pts_instance::permit_open(std::shared_ptr<vfs::file>)
    {
        const auto pty_pair = find_pair(minor);
        if (!pty_pair)
            return std::unexpected { lib::err::no_such_device };
        if (pty_pair->locked.load(std::memory_order_acquire))
            return std::unexpected { lib::err::io_error };
        if (auto peer = link.lock())
            peer->hung_up.store(false, std::memory_order_release);
        return { };
    }

    lib::expect<int> ptm_instance::ioctl(std::uint64_t request, lib::uptr_or_addr argp)
    {
        switch (request)
        {
            case tiocgptn:
            {
                const auto value = static_cast<unsigned int>(minor);
                if (!argp.write(value))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case tiocsptlck:
            {
                int value = 0;
                if (!argp.read(value))
                    return std::unexpected { lib::err::invalid_address };
                const auto pty_pair = find_pair(minor);
                if (!pty_pair)
                    return std::unexpected { lib::err::no_such_device };
                pty_pair->locked.store(value != 0, std::memory_order_release);
                return 0;
            }
            case tiocgptlck:
            {
                const auto pty_pair = find_pair(minor);
                if (!pty_pair)
                    return std::unexpected { lib::err::no_such_device };
                const int value = pty_pair->locked.load(std::memory_order_acquire) ? 1 : 0;
                if (!argp.write(value))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case tiocgptpeer:
            {
                const int open_flags = static_cast<int>(argp.address());
                const auto proc = sched::current_process();
                if (!proc || !proc->fdt)
                    return std::unexpected { lib::err::io_error };

                auto resolved = vfs::resolve(std::nullopt, fmt::format("/dev/pts/{}", minor));
                if (!resolved.has_value())
                    return std::unexpected { resolved.error() };

                auto slave_fdesc = vfs::filedesc::create(resolved->target, open_flags);
                const auto fd_num = proc->fdt->alloc(
                    slave_fdesc, 0, false,
                    proc->rlimits->get(sched::rlimit_nofile).cur
                );
                if (fd_num < 0)
                    return fd_num;

                if (const auto open_res = slave_fdesc->file->open(open_flags, proc->pid); !open_res)
                {
                    proc->fdt->close(fd_num);
                    return std::unexpected { open_res.error() };
                }
                return fd_num;
            }
            case tty::ioctl::tcgets:
            case tty::ioctl::tcsets:
            case tty::ioctl::tcsetsw:
            case tty::ioctl::tcsetsf:
            case tty::ioctl::tcgets2:
            case tty::ioctl::tcsets2:
            case tty::ioctl::tcsetsw2:
            case tty::ioctl::tcsetsf2:
            {
                auto peer = link.lock();
                if (!peer)
                    return std::unexpected { lib::err::io_error };
                return peer->ioctl(request, argp);
            }
            case tty::ioctl::tiocswinsz:
            {
                const auto base_res = tty::instance::ioctl(request, argp);
                if (!base_res.has_value())
                    return base_res;
                if (auto peer = link.lock())
                {
                    propagate_winsize(*this, *peer);
                    signal_winch(*peer);
                }
                return base_res;
            }
            default:
                return tty::instance::ioctl(request, argp);
        }
    }

    lib::expect<int> pts_instance::ioctl(std::uint64_t request, lib::uptr_or_addr argp)
    {
        if (request == tty::ioctl::tiocswinsz)
        {
            const auto base_res = tty::instance::ioctl(request, argp);
            if (!base_res.has_value())
                return base_res;
            if (auto peer = link.lock())
                propagate_winsize(*this, *peer);
            signal_winch(*this);
            return base_res;
        }
        return tty::instance::ioctl(request, argp);
    }

    lib::expect<std::shared_ptr<pair>> alloc()
    {
        if (ptm == nullptr || pts == nullptr)
            return std::unexpected { lib::err::no_such_device };

        std::uint32_t pty_minor;
        {
            auto state = allocator.lock();
            const auto first_free = std::ranges::find(state->minor_used, false);
            if (first_free == state->minor_used.end())
                return std::unexpected { lib::err::no_space_left };
            *first_free = true;
            pty_minor = static_cast<std::uint32_t>(
                std::distance(state->minor_used.begin(), first_free)
            );
        }

        auto pty_pair = std::make_shared<pair>();
        pty_pair->minor = pty_minor;
        pty_pair->master = std::make_shared<ptm_instance>(ptm, pty_minor);
        pty_pair->slave = std::make_shared<pts_instance>(pts, pty_minor);
        pty_pair->master->link = pty_pair->slave;
        pty_pair->slave->link = pty_pair->master;

        ptm->instances.lock()->emplace(pty_minor, pty_pair->master);
        pts->instances.lock()->emplace(pty_minor, pty_pair->slave);

        if (auto master_ld = pty_pair->master->ldisc.lock().value())
            master_ld->open();
        if (auto slave_ld = pty_pair->slave->ldisc.lock().value())
            slave_ld->open();

        const auto attach = devpts::attach_slave(
            pty_minor, slave_mode, vfs::dev::makedev(slave_major, pty_minor)
        );
        if (!attach.has_value())
        {
            ptm->instances.lock()->erase(pty_minor);
            pts->instances.lock()->erase(pty_minor);
            allocator.lock()->minor_used[pty_minor] = false;
            return std::unexpected { attach.error() };
        }

        allocator.lock()->pairs.emplace(pty_minor, pty_pair);
        return pty_pair;
    }

    void release(std::uint32_t pty_minor)
    {
        if (pty_minor >= master_count)
            return;

        std::shared_ptr<pair> pty_pair;
        {
            auto state = allocator.lock();
            if (!state->minor_used[pty_minor])
                return;
            if (auto iter = state->pairs.find(pty_minor); iter != state->pairs.end())
            {
                pty_pair = std::move(iter->second);
                state->pairs.erase(iter);
            }
        }

        if (const auto ret = devpts::detach_slave(pty_minor); !ret)
            lib::warn("pty: detach slave {} failed: {}", pty_minor, lib::error_name(ret.error()));

        if (pty_pair)
        {
            if (auto ld = pty_pair->master->ldisc.lock().value())
                ld->shutdown();
            if (auto ld = pty_pair->slave->ldisc.lock().value())
                ld->shutdown();

            ptm->instances.lock()->erase(pty_minor);

            auto pts_locked = pts->instances.lock();
            if (pty_pair->slave->ref.load(std::memory_order_acquire) == 0)
                pts_locked->erase(pty_minor);
        }

        allocator.lock()->minor_used[pty_minor] = false;
    }

    namespace
    {
        struct ptmx_ops : vfs::ops
        {
            static std::shared_ptr<ptmx_ops> singleton()
            {
                static auto inst = std::make_shared<ptmx_ops>();
                return inst;
            }

            bool seekable() const override { return false; }

            lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags, pid_t pid) override
            {
                lib::unused(flags, pid);
                lib::bug_on(!file || file->private_data != nullptr);

                auto pair_res = pty::alloc();
                if (!pair_res.has_value())
                    return std::unexpected { pair_res.error() };

                auto pty_pair = std::move(*pair_res);
                auto master = pty_pair->master;

                if (const auto ret = master->open(file); !ret)
                {
                    pty::release(pty_pair->minor);
                    return ret;
                }

                master->ref.store(1, std::memory_order_relaxed);
                file->private_data = master;
                return { };
            }

            lib::expect<void> close(vfs::file &file) override
            {
                lib::bug_on(!file.private_data);

                const auto master = std::static_pointer_cast<tty::instance>(file.private_data);
                const auto prev_ref = master->ref.fetch_sub(1, std::memory_order_acq_rel);
                lib::bug_on(prev_ref == 0);

                if (prev_ref == 1)
                {
                    if (const auto close_res = master->close(); !close_res)
                    {
                        master->ref.fetch_add(1, std::memory_order_relaxed);
                        return close_res;
                    }
                    const auto pty_minor = master->minor;
                    file.private_data.reset();
                    pty::release(pty_minor);
                }
                else file.private_data.reset();

                return { };
            }

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);
                const auto master = std::static_pointer_cast<tty::instance>(file->private_data);
                return master->read(std::move(file), buffer);
            }

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);
                const auto master = std::static_pointer_cast<tty::instance>(file->private_data);
                return master->write(std::move(file), buffer);
            }

            lib::expect<int> ioctl(
                std::shared_ptr<vfs::file> file, std::uint64_t request,
                lib::uptr_or_addr argp
            ) override
            {
                const auto master = std::static_pointer_cast<tty::instance>(file->private_data);
                return master->ioctl(request, argp);
            }

            lib::expect<std::uint16_t> poll(
                std::shared_ptr<vfs::file> file, vfs::poll_table *poll_tab
            ) override
            {
                const auto master = std::static_pointer_cast<tty::instance>(file->private_data);
                return master->poll(poll_tab);
            }

            lib::expect<void> trunc(std::shared_ptr<vfs::file>, std::size_t) override
            {
                return { };
            }
        };
    } // namespace

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.dev.pty.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task register_task
    {
        "vfs.dev.pty.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { devtmpfs::registered_stage() },
        lib::initgraph::entail { registered_stage() },
        [] {
            ptm = new ptm_driver;
            pts = new pts_driver;
            ptm->other = pts;
            pts->other = ptm;
            tty::register_driver(ptm);
            tty::register_driver(pts);

            for (std::uint32_t idx = 0; idx < master_count; idx++)
            {
                tty::register_chrdev(vfs::dev::makedev(master_major, idx));
                tty::register_chrdev(vfs::dev::makedev(slave_major, idx));
            }

            constexpr auto ptmx_rdev = vfs::dev::makedev(5, 2);
            vfs::dev::register_dev_ops(ptmx_rdev, ptmx_ops::singleton());
            if (const auto ret = devtmpfs::create("ptmx", stat::s_ifchr | 0666, ptmx_rdev); !ret)
            {
                lib::panic(
                    "pty: failed to create '/dev/ptmx': {}",
                    lib::error_name(ret.error())
                );
            }
        }
    };
} // namespace fs::dev::pty
