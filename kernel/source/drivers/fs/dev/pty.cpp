// Copyright (C) 2024-2026  ilobilo

module drivers.fs.dev.pty;

import drivers.fs.devtmpfs;
import drivers.fs.devpts;
import drivers.fs.dev.tty;
import drivers.dev;
import system.sched.mutex;
import system.sched;
import system.vfs.dev;
import fmt;

namespace fs::dev::pty
{
    namespace
    {
        constexpr std::size_t master_count = 4096;
        constexpr std::uint32_t master_major = 128;
        constexpr std::uint32_t slave_major = 136;

        using namespace lib::ioc;
        enum ioctls
        {
            tiocpkt = 0x5420,
            tiocgptn = make_ior<unsigned int>('T', 0x30),
            tiocsptlck = make_iow<int>('T', 0x31),
            tiocsig = make_iow<int>('T', 0x36),
            tiocgpkt = make_ior<int>('T', 0x38),
            tiocgptlck = make_ior<int>('T', 0x39),
            tiocgptpeer = make_io('T', 0x41)
        };

        enum pkt : std::uint8_t
        {
            pkt_data = 0,
            pkt_flushread = 1,
            pkt_flushwrite = 2,
            pkt_stop = 4,
            pkt_start = 8,
            pkt_nostop = 16,
            pkt_dostop = 32,
            pkt_ioctl = 64
        };

        constexpr mode_t slave_mode = stat::s_ifchr | 0620;
        constexpr tty::ktermios slave_termios = tty::ktermios::standard();
        constexpr tty::ktermios master_termios = [] {
            auto tios = tty::ktermios::standard();
            tios.c_iflag = 0;
            tios.c_oflag = 0;
            tios.c_lflag = 0;
            return tios;
        } ();
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
        lib::locker<allocator_t, sched::mutex_t> allocator;

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

    } // namespace

    struct pty_instance_base : tty::instance
    {
        // TODO: stored in two places
        std::shared_ptr<tty::default_ldisc> ld;

        pty_instance_base(tty::driver *drv, std::uint32_t pty_minor)
            : instance { drv, pty_minor, std::make_shared<tty::default_ldisc>(this) },
              ld { std::static_pointer_cast<tty::default_ldisc>(ldisc.lock().value()) } { }

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

        lib::expect<void> open(std::shared_ptr<vfs::file_t>) override { return { }; }

        lib::expect<void> close() override
        {
            hangup_peer(*this);
            return { };
        }

        bool needs_close_erase() const override
        {
            return find_pair(minor) == nullptr;
        }

        void resize(const tty::winsize &size) override
        {
            tty::instance::resize(size);
            if (auto peer = link.lock())
                peer->winsize.lock().value() = size;
        }
    };

    struct ptm_instance : pty_instance_base
    {
        using pty_instance_base::pty_instance_base;

        std::atomic_bool packet = false;

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file_t> file, lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<int> ioctl(std::uint64_t request, lib::uptr_or_addr argp) override;
        lib::expect<std::uint16_t> poll(vfs::poll_table_t *pt) override;
    };

    struct pts_instance : pty_instance_base
    {
        using pty_instance_base::pty_instance_base;

        ~pts_instance() { allocator.lock()->minor_used[minor] = false; }

        std::atomic<std::uint8_t> pktstatus = 0;
        void raise(std::uint8_t set, std::uint8_t clear = 0);

        void flush_notify(int queue) override;
        void flow_notify(bool stop) override;
        void set_termios(tty::ktermios &current, const tty::ktermios &old) override;

        lib::expect<void> permit_open(std::shared_ptr<vfs::file_t>) override;
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

    void pts_instance::raise(std::uint8_t set, std::uint8_t clear)
    {
        const auto master = std::static_pointer_cast<ptm_instance>(link.lock());
        if (!master || !master->packet.load(std::memory_order_acquire))
            return;

        auto expected = pktstatus.load(std::memory_order_relaxed);
        while (!pktstatus.compare_exchange_weak(
            expected, static_cast<std::uint8_t>((expected & ~clear) | set),
            std::memory_order_acq_rel, std::memory_order_relaxed)) { }

        master->ld->in_wq.wake_all();
    }

    void pts_instance::flush_notify(int queue)
    {
        switch (queue)
        {
            case tty::tciflush:
                raise(pkt_flushread);
                break;
            case tty::tcoflush:
                raise(pkt_flushwrite);
                break;
            case tty::tcioflush:
                raise(pkt_flushread | pkt_flushwrite);
                break;
        }
    }

    void pts_instance::flow_notify(bool stop)
    {
        if (stop)
            raise(pkt_stop, pkt_start);
        else
            raise(pkt_start, pkt_stop);
    }

    void pts_instance::set_termios(tty::ktermios &current, const tty::ktermios &old)
    {
        using enum tty::ktermios::iflag;
        using enum tty::ktermios::cc;

        const auto flow = [](const tty::ktermios &tios) {
            return (tios.c_iflag & ixon) != 0 &&
                tios.c_cc[vstop] == 0x13 && tios.c_cc[vstart] == 0x11;
        };

        if (flow(current) == flow(old))
            return;
        raise(flow(current) ? pkt_dostop : pkt_nostop, pkt_dostop | pkt_nostop);
    }

    lib::expect<void> pts_instance::permit_open(std::shared_ptr<vfs::file_t>)
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

    lib::expect<std::size_t> ptm_instance::read(
        std::shared_ptr<vfs::file_t> file, lib::maybe_uspan<std::byte> buffer
    )
    {
        if (!packet.load(std::memory_order_acquire))
            return tty::instance::read(std::move(file), buffer);

        if (buffer.size() == 0)
            return 0uz;

        const auto slave = std::static_pointer_cast<pts_instance>(link.lock());
        const bool nonblock = (file->flags & vfs::o_nonblock) != 0;

        const auto put = [&](std::uint8_t value) {
            const auto byte = static_cast<std::byte>(value);
            return buffer.subspan(0, 1).copy_from(std::span { &byte, 1 });
        };

        while (true)
        {
            const auto gen = ld->in_wq.snapshot_gen();

            if (slave)
            {
                if (const auto status = slave->pktstatus.exchange(0, std::memory_order_acq_rel))
                {
                    if (!put(status))
                        return std::unexpected { lib::err::invalid_address };
                    return 1uz;
                }
            }

            if (ld->maybe_readable())
                break;

            if (nonblock)
                return std::unexpected { lib::err::try_again };

            const auto res = ld->in_wq.wait_prepared(gen);
            if (res.interrupted || res.killed)
                return std::unexpected { lib::err::interrupted };
        }

        if (buffer.size() == 1)
        {
            if (!put(pkt_data))
                return std::unexpected { lib::err::invalid_address };
            return 1uz;
        }

        const auto ret = tty::instance::read(std::move(file), buffer.subspan(1));
        if (!ret || *ret == 0)
            return ret;

        if (!put(pkt_data))
            return std::unexpected { lib::err::invalid_address };
        return *ret + 1;
    }

    lib::expect<int> ptm_instance::ioctl(std::uint64_t request, lib::uptr_or_addr argp)
    {
        switch (request)
        {
            case tiocpkt:
            {
                int value = 0;
                if (!argp.read(value))
                    return std::unexpected { lib::err::invalid_address };

                if (value == 0)
                    packet.store(false, std::memory_order_release);
                else if (!packet.exchange(true, std::memory_order_acq_rel))
                {
                    if (const auto slave = std::static_pointer_cast<pts_instance>(link.lock()))
                        slave->pktstatus.store(0, std::memory_order_release);
                }
                return 0;
            }
            case tiocgpkt:
            {
                const int value = packet.load(std::memory_order_acquire) ? 1 : 0;
                if (!argp.write(value))
                    return std::unexpected { lib::err::invalid_address };
                return 0;
            }
            case tiocsig:
            {
                const int sig = argp.value();
                if (sig != sched::sigint && sig != sched::sigquit && sig != sched::sigtstp)
                    return std::unexpected { lib::err::invalid_argument };

                if (const auto slave = link.lock())
                {
                    if (const auto group = slave->ctrl.lock()->group.lock())
                        group->signal_all(sig, true);
                }
                return 0;
            }
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
                const int open_flags = argp.value();
                const auto proc = sched::current_process();
                if (!proc || !proc->fdt)
                    return std::unexpected { lib::err::io_error };

                auto resolved = vfs::resolve(std::nullopt, fmt::format("/dev/pts/{}", minor));
                if (!resolved.has_value())
                    return std::unexpected { resolved.error() };

                auto slave_fdesc = vfs::filedesc::create(resolved->target, open_flags);
                const auto fdres = proc->fdt->alloc(
                    slave_fdesc, 0, false,
                    proc->rlimits->get(sched::rlimit_nofile).cur
                );
                if (!fdres.has_value())
                    return fdres;

                if (const auto open_res = slave_fdesc->file->open(open_flags, proc->pid); !open_res)
                {
                    proc->fdt->close(*fdres);
                    return std::unexpected { open_res.error() };
                }
                return *fdres;
            }
            default:
                return tty::instance::ioctl(request, argp);
        }
    }

    lib::expect<std::uint16_t> ptm_instance::poll(vfs::poll_table_t *pt)
    {
        auto events = tty::instance::poll(pt);
        if (!events || !packet.load(std::memory_order_acquire))
            return events;

        const auto slave = std::static_pointer_cast<pts_instance>(link.lock());
        if (slave && slave->pktstatus.load(std::memory_order_acquire) != 0)
            *events |= vfs::pollin | vfs::pollrdnorm;

        return events;
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
            pty_minor, slave_mode, makedev(slave_major, pty_minor)
        );
        if (!attach.has_value())
        {
            ptm->instances.lock()->erase(pty_minor);
            pts->instances.lock()->erase(pty_minor);
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
    }

    namespace
    {
        struct ptmx_ops : vfs::ops_t
        {
            static std::shared_ptr<ptmx_ops> singleton()
            {
                static auto inst = std::make_shared<ptmx_ops>();
                return inst;
            }

            bool seekable() const override { return false; }

            lib::expect<void> open(std::shared_ptr<vfs::file_t> file, int flags, pid_t pid) override
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

            lib::expect<void> close(vfs::file_t &file) override
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
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);
                const auto master = std::static_pointer_cast<tty::instance>(file->private_data);
                return master->read(std::move(file), buffer);
            }

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);
                const auto master = std::static_pointer_cast<tty::instance>(file->private_data);
                return master->write(std::move(file), buffer);
            }

            lib::expect<int> ioctl(
                std::shared_ptr<vfs::file_t> file, std::uint64_t request,
                lib::uptr_or_addr argp
            ) override
            {
                const auto master = std::static_pointer_cast<tty::instance>(file->private_data);
                return master->ioctl(request, argp);
            }

            lib::expect<std::uint16_t> poll(
                std::shared_ptr<vfs::file_t> file, vfs::poll_table_t *poll_tab
            ) override
            {
                const auto master = std::static_pointer_cast<tty::instance>(file->private_data);
                return master->poll(poll_tab);
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
        lib::initgraph::require {
            devtmpfs::mounted_stage(),
            ::dev::available_stage()
        },
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
                tty::register_chrdev(makedev(master_major, idx));
                tty::register_chrdev(makedev(slave_major, idx));
            }

            tty::register_device("ptmx", makedev(5, 2), ptmx_ops::singleton());
        }
    };
} // namespace fs::dev::pty
