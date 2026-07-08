// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.chrono;

namespace syscall::vfs
{
    using namespace ::vfs;

    union epoll_data_t
    {
        void *ptr;
        int fd;
        std::uint32_t u32;
        std::uint64_t u64;
    };

    struct [[gnu::packed]] epoll_event
    {
        std::uint32_t events;
        epoll_data_t data;
    };

    namespace
    {
        constexpr int epoll_cloexec = o_cloexec;

        enum ctls
        {
            epoll_ctl_add = 1,
            epoll_ctl_del = 2,
            epoll_ctl_mod = 3
        };

        enum epolls : std::uint32_t
        {
            epollexclusive = 1u << 28,
            epollwakeup = 1u << 29,
            epolloneshot = 1u << 30,
            epollet = 1u << 31
        };

        constexpr std::uint32_t valid_events = 0xFFFFu |
            epollexclusive | epollwakeup | epolloneshot | epollet;

        struct epoll_instance_t;
        struct epoll_entry_t;
        void do_signal(epoll_entry_t *ent);

        struct epoll_entry_t
        {
            epoll_instance_t *epi;
            int fd;
            std::weak_ptr<file> wfile;
            std::uint32_t events;
            epoll_data_t data;
            bool active;

            bool on_ready;
            lib::intrusive_list_hook<epoll_entry_t> hook;

            struct reg
            {
                sched::wait_queue_t *wq;
                sched::wait_queue_entry_t entry;

                reg(epoll_entry_t *ent, sched::wait_queue_t *wq)
                    : wq { wq }, entry {
                        [ent]{ do_signal(ent); },
                        (ent->events & epollexclusive) != 0
                    } { }
            };
            lib::list<reg> regs;
        };

        struct epoll_instance_t
        {
            sched::wait_queue_t bell;
            sched::mutex lock;
            lib::spinlock_irq ready_lock;

            lib::map::flat_hash<
                int,
                std::shared_ptr<epoll_entry_t>
            > watched;

            lib::intrusive_list<
                epoll_entry_t,
                &epoll_entry_t::hook
            > ready;

            ~epoll_instance_t()
            {
                for (auto &[fd, ent] : watched)
                {
                    for (auto &reg : ent->regs)
                        reg.wq->remove_entry(reg.entry);
                }
            }
        };

        void do_signal(epoll_entry_t *ent)
        {
            auto epi = ent->epi;
            {
                const std::unique_lock _ { epi->ready_lock };
                if (!ent->active)
                    return;

                if (!ent->on_ready)
                {
                    ent->on_ready = true;
                    epi->ready.push_back(ent);
                }
            }
            epi->bell.wake_all();
        }

        struct epoll_table : ::vfs::poll_table
        {
            epoll_entry_t *ent;

            explicit epoll_table(epoll_entry_t *ent)
                : ent { ent } { }

            void add(sched::wait_queue_t &wq) override
            {
                auto &reg = ent->regs.emplace_back(ent, &wq);
                wq.add_entry(reg.entry);
            }
        };

        std::uint16_t interest_mask(const epoll_entry_t *ent)
        {
            return static_cast<std::uint16_t>(ent->events & 0xFFFFu) | pollerr | pollhup;
        }

        std::optional<std::uint32_t> rearm(epoll_entry_t *ent)
        {
            for (auto &reg : ent->regs)
                reg.wq->remove_entry(reg.entry);
            ent->regs.clear();

            auto file = ent->wfile.lock();
            if (!file)
                return std::nullopt;

            epoll_table pt { ent };
            const auto res = file->poll(&pt);
            if (!res)
                return 0;

            return static_cast<std::uint32_t>(*res) & interest_mask(ent);
        }

        void drop_ready(epoll_instance_t *epi, epoll_entry_t *ent)
        {
            const std::unique_lock _ { epi->ready_lock };
            if (ent->on_ready)
            {
                epi->ready.remove(ent);
                ent->on_ready = false;
            }
        }

        struct ops : ::vfs::ops
        {
            static std::shared_ptr<ops> singleton()
            {
                static auto instance = std::make_shared<ops>();
                return instance;
            }

            bool seekable() const override { return false; }

            lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags, pid_t pid) override
            {
                lib::unused(file, flags, pid);
                return std::unexpected { lib::err::invalid_device_or_address };
            }

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(file, offset, buffer);
                return std::unexpected { lib::err::invalid_argument };
            }

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(file, offset, buffer);
                return std::unexpected { lib::err::invalid_argument };
            }

            lib::expect<std::uint16_t> poll(
                std::shared_ptr<vfs::file> file, vfs::poll_table *pt
            ) override
            {
                auto epi = std::static_pointer_cast<epoll_instance_t>(file->private_data);
                if (pt)
                    pt->add(epi->bell);

                {
                    const std::unique_lock _ { epi->ready_lock };
                    if (!epi->ready.empty())
                        return pollin;
                }

                std::vector<std::shared_ptr<epoll_entry_t>> snapshot;
                {
                    const std::unique_lock _ { epi->lock };
                    snapshot.reserve(epi->watched.size());
                    for (auto &[fd, ent] : epi->watched)
                        snapshot.push_back(ent);
                }

                for (auto &ent : snapshot)
                {
                    if (!ent->active)
                        continue;

                    auto file = ent->wfile.lock();
                    if (!file)
                        continue;

                    const auto res = file->poll(nullptr);
                    if (res && (*res & interest_mask(ent.get())))
                        return pollin;
                }
                return 0;
            }
        };

        std::shared_ptr<epoll_instance_t> get_epi(int epfd)
        {
            auto desc = sched::current_process()->fdt->get(epfd);
            if (!desc || !desc->file)
                return nullptr;
            if (desc->file->ops.get() != ops::singleton().get())
                return nullptr;
            return std::static_pointer_cast<epoll_instance_t>(desc->file->private_data);
        }

        int do_epoll_wait(
            const std::shared_ptr<epoll_instance_t> &epi,
            std::vector<epoll_event> &out, int maxevents,
            bool has_timeout, std::uint64_t timeout_ns
        )
        {
            const auto timer = chrono::main_timer();
            std::uint64_t deadline_ns = 0;
            if (has_timeout)
                deadline_ns = timer->ns() + timeout_ns;

            while (true)
            {
                const auto gen = epi->bell.snapshot_gen();
                {
                    const std::unique_lock _ { epi->lock };

                    std::vector<epoll_entry_t *> batch;
                    {
                        const std::unique_lock _ { epi->ready_lock };
                        while (!epi->ready.empty() &&
                               static_cast<int>(batch.size() + out.size()) < maxevents)
                        {
                            auto ent = epi->ready.pop_front();
                            ent->on_ready = false;
                            batch.push_back(ent);
                        }
                    }

                    for (auto ent : batch)
                    {
                        if (!ent->active)
                            continue;

                        const auto rev = rearm(ent);
                        if (!rev)
                        {
                            epi->watched.erase(ent->fd);
                            continue;
                        }
                        if (*rev == 0)
                            continue;

                        out.push_back({ *rev, ent->data });

                        if (ent->events & epolloneshot)
                            ent->active = false;
                        else if (!(ent->events & epollet))
                        {
                            const std::unique_lock _ { epi->ready_lock };
                            if (!ent->on_ready)
                            {
                                ent->on_ready = true;
                                epi->ready.push_back(ent);
                            }
                        }
                    }
                }

                if (!out.empty())
                    return out.size();

                std::uint64_t wait_ns = 0;
                if (has_timeout)
                {
                    const auto now = timer->ns();
                    if (now >= deadline_ns)
                        return 0;
                    wait_ns = deadline_ns - now;
                }

                const auto wr = epi->bell.wait_prepared(gen, wait_ns);
                if (wr.expired)
                    return 0;
                if (wr.interrupted || wr.killed)
                    return -EINTR;
            }
        }

        int epoll_wait_common(
            int epfd, epoll_event __user *events, int maxevents,
            bool has_timeout, std::uint64_t timeout_ns
        )
        {
            if (maxevents <= 0)
                return -EINVAL;
            if (events == nullptr)
                return -EFAULT;

            auto epi = get_epi(epfd);
            if (!epi)
                return -EBADF;

            std::vector<epoll_event> kevents;
            kevents.reserve(maxevents);

            const auto ret = do_epoll_wait(epi, kevents, maxevents, has_timeout, timeout_ns);
            if (ret < 0)
                return ret;

            for (std::size_t i = 0; i < kevents.size(); i++)
            {
                if (!lib::copy_to_user(&events[i], &kevents[i], sizeof(epoll_event)))
                    return -EFAULT;
            }
            return ret;
        }

        int epoll_pwait_common(
            int epfd, epoll_event __user *events, int maxevents,
            bool has_timeout, std::uint64_t timeout_ns,
            const void __user *sigmask, std::size_t sigsetsize
        )
        {
            sched::scoped_sigmask guard;
            sched::sigset_t kmask { };
            if (sigmask)
            {
                if (sigsetsize != sizeof(sched::sigset_t))
                    return -EINVAL;
                if (!lib::copy_from_user(&kmask, sigmask, sizeof(kmask)))
                    return -EFAULT;
                guard.apply(&kmask);
            }

            const auto ret = epoll_wait_common(epfd, events, maxevents, has_timeout, timeout_ns);
            if (ret == -EINTR)
                guard.disarm();
            return ret;
        }
    } // namespace

    int epoll_create1(int flags)
    {
        if (flags & ~epoll_cloexec)
            return -EINVAL;

        auto ret = create_anon_fd({
            .name = "<[EPOLL]>",
            .ops = ops::singleton(),
            .file_private_data = std::make_shared<epoll_instance_t>(),
            .inode_private_data = nullptr,
            .st_mode = std::to_underlying(stat::s_ifreg) | s_irusr | s_iwusr,
            .flags = flags,
            .skip_open = true,
            .inode = nullptr
        });
        if (!ret)
            return -lib::map_error(ret.error());

        return ret->first;
    }

    int epoll_create(int size)
    {
        if (size <= 0)
            return -EINVAL;
        return epoll_create1(0);
    }

    int epoll_ctl(int epfd, int op, int fd, epoll_event __user *event)
    {
        if (op != epoll_ctl_add && op != epoll_ctl_mod && op != epoll_ctl_del)
            return -EINVAL;

        if (op != epoll_ctl_del && fd == epfd)
            return -EINVAL;

        auto epi = get_epi(epfd);
        if (!epi)
            return -EBADF;

        auto desc = sched::current_process()->fdt->get(fd);
        if (!desc || !desc->file)
            return -EBADF;

        epoll_event ev { };
        if (op != epoll_ctl_del)
        {
            if (!event || !lib::copy_from_user(&ev, event, sizeof(epoll_event)))
                return -EFAULT;
            if (ev.events & ~valid_events)
                return -EINVAL;

            if (ev.events & epollexclusive)
            {
                if (op == epoll_ctl_mod || (ev.events & epolloneshot))
                    return -EINVAL;
                if (desc->file->ops.get() == ops::singleton().get())
                    return -EINVAL;
            }

            // TODO: should keep system awake while an event is pending
            ev.events &= ~epollwakeup;
        }

        const std::unique_lock _ { epi->lock };
        const auto it = epi->watched.find(fd);
        const bool present = (it != epi->watched.end());

        epoll_entry_t *target = nullptr;

        switch (op)
        {
            case epoll_ctl_add:
            {
                if (present)
                    return -EEXIST;

                auto ent = std::make_shared<epoll_entry_t>();
                ent->epi = epi.get();
                ent->fd = fd;
                ent->wfile = desc->file;
                ent->events = ev.events;
                ent->data = ev.data;
                ent->active = true;
                ent->on_ready = false;
                target = ent.get();
                epi->watched.emplace(fd, std::move(ent));
                break;
            }
            case epoll_ctl_mod:
            {
                if (!present)
                    return -ENOENT;

                auto &ent = it->second;
                ent->events = ev.events;
                ent->data = ev.data;
                ent->active = true;
                target = ent.get();
                drop_ready(epi.get(), target);
                break;
            }
            case epoll_ctl_del:
            {
                if (!present)
                    return -ENOENT;

                auto &ent = it->second;
                for (auto &reg : ent->regs)
                    reg.wq->remove_entry(reg.entry);
                ent->regs.clear();
                drop_ready(epi.get(), ent.get());
                epi->watched.erase(it);
                return 0;
            }
        }

        const auto rev = rearm(target);
        if (rev && *rev != 0)
            do_signal(target);
        return 0;
    }

    int epoll_pwait2(
        int epfd, epoll_event __user *events, int maxevents, const timespec __user *timeout,
        const sigset_t __user *sigmask, std::size_t sigsetsize
    )
    {
        bool has_timeout = false;
        std::uint64_t timeout_ns = 0;
        if (timeout != nullptr)
        {
            timespec ktimeout;
            if (!lib::copy_from_user(&ktimeout, timeout, sizeof(timespec)))
                return -EFAULT;

            if (ktimeout.tv_nsec < 0 || ktimeout.tv_nsec >= 1'000'000'000l || ktimeout.tv_sec < 0)
                return -EINVAL;

            has_timeout = true;
            timeout_ns = ktimeout.to_ns();
        }

        return epoll_pwait_common(
            epfd, events, maxevents, has_timeout, timeout_ns, sigmask, sigsetsize
        );
    }

    int epoll_pwait(
        int epfd, epoll_event __user *events, int maxevents, int timeout,
        const sigset_t __user *sigmask, std::size_t sigsetsize
    )
    {
        const bool has_timeout = (timeout >= 0);
        const std::uint64_t timeout_ns = has_timeout ?
            static_cast<std::uint64_t>(timeout) * 1'000'000ul : 0;

        return epoll_pwait_common(
            epfd, events, maxevents, has_timeout, timeout_ns, sigmask, sigsetsize
        );
    }

    int epoll_wait(int epfd, epoll_event __user *events, int maxevents, int timeout)
    {
        const bool has_timeout = (timeout >= 0);
        const std::uint64_t timeout_ns = has_timeout
            ? static_cast<std::uint64_t>(timeout) * 1'000'000ul : 0;

        return epoll_wait_common(epfd, events, maxevents, has_timeout, timeout_ns);
    }
} // namespace syscall::vfs
