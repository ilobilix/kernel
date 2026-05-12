// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.chrono;

namespace syscall::vfs
{
    using namespace ::vfs;

    constexpr int FD_SETSIZE = 1024;
    struct [[aligned(alignof(long))]] fd_set
    {
        std::uint8_t fds_bits[FD_SETSIZE / 8];
    };
    static_assert(sizeof(fd_set) == FD_SETSIZE / 8);

    struct sigset_t
    {
        unsigned long sig[1024 / (8 * sizeof(long))];
    };

    namespace
    {
        // inline void FD_CLR(int fd, fd_set *set)
        // {
        //     lib::bug_on(fd >= FD_SETSIZE);
        //     set->fds_bits[fd / 8] &= ~(1 << (fd % 8));
        // }

        inline int FD_ISSET(int fd, const fd_set *set)
        {
            lib::bug_on(fd >= FD_SETSIZE);
            return set->fds_bits[fd / 8] & (1 << (fd % 8));
        }

        inline void FD_SET(int fd, fd_set *set) {
            lib::bug_on(fd >= FD_SETSIZE);
            set->fds_bits[fd / 8] |= 1 << (fd % 8);
        }

        inline void FD_ZERO(fd_set *set)
        {
            std::memset(set->fds_bits, 0, sizeof(fd_set));
        }

        struct poll_table : ::vfs::poll_table
        {
            struct entry
            {
                sched::wait_queue_entry_t entry;
                sched::wait_queue_t *wq;
            };

            lib::list<entry> entries;

            poll_table() = default;
            ~poll_table()
            {
                for (auto &entry : entries)
                    entry.wq->remove_entry(entry.entry);
            }

            void add(sched::wait_queue_t &wq) override
            {
                auto &entry = entries.emplace_back();
                entry.wq = &wq;
                wq.add_entry(entry.entry);
            }
        };

        int ppoll(std::vector<pollfd> &fds, timespec *timeout, const sigset_t *sigmask)
        {
            std::uint64_t timeout_ns = 0;
            if (timeout)
            {
                if (timeout->tv_nsec < 0 || timeout->tv_nsec >= 1'000'000'000l)
                    return -EINVAL;

                timeout_ns = timeout->to_ns();
            }

            auto thread = sched::current_thread();
            auto process = thread->proc;

            // TODO
            lib::unused(sigmask);
            // sigset_t old_sigmask;
            // if (sigmask)
            //     sigprocmask(SIG_SETMASK, sigmask, &old_sigmask);

            auto cleanup = [&] {
                // TODO
                // if (sigmask)
                //     restore old sigmask
            };

            struct fd_slot
            {
                std::shared_ptr<file> file;
                std::uint16_t events;
            };

            std::vector<fd_slot> slots(fds.size());

            for (nfds_t i = 0; i < fds.size(); i++)
            {
                fds[i].revents = 0;
                if (fds[i].fd < 0)
                {
                    slots[i].file = nullptr;
                    continue;
                }

                auto desc = process->fdt->get(fds[i].fd);
                if (!desc)
                {
                    fds[i].revents = pollnval;
                    slots[i].file = nullptr;
                    continue;
                }

                slots[i].file = desc->file;
                slots[i].events = fds[i].events | pollerr | pollhup;
            }

            while (true)
            {
                poll_table pt;
                std::size_t ready = 0;

                for (nfds_t i = 0; i < fds.size(); i++)
                {
                    if (!slots[i].file)
                        continue;

                    const auto &ops = slots[i].file->ops;
                    if (!ops)
                    {
                        fds[i].revents = pollerr;
                        ready++;
                        continue;
                    }

                    auto res = ops->poll(slots[i].file, &pt);
                    if (res.has_value())
                    {
                        fds[i].revents = *res & slots[i].events;
                        if (fds[i].revents)
                            ready++;
                    }
                }

                if (ready > 0)
                {
                    cleanup();
                    return ready;
                }

                if (timeout && timeout_ns == 0)
                {
                    cleanup();
                    return 0;
                }

                thread->state.store(sched::thread_state::sleeping, std::memory_order_release);
                sched::sleep_entry_t sleep_timeout {
                    .thread = thread,
                    .deadline_ns = 0,
                    .expired = false,
                    .hook = { }
                };

                if (timeout)
                    sched::arm_thread_timeout(&sleep_timeout, timeout_ns);

                const bool interrupted = sched::yield();
                if (timeout)
                {
                    if (sleep_timeout.expired)
                    {
                        cleanup();
                        return 0;
                    }
                    else sched::cancel_thread_timeout(&sleep_timeout);
                }

                if (interrupted)
                {
                    cleanup();
                    return -EINTR;
                }
            }
        }

        int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, timespec *timeout, bool update_timeout, const sigset_t *sigmask)
        {
            if (nfds < 0 || nfds > FD_SETSIZE)
                return -EINVAL;

            std::vector<pollfd> pfds;
            pfds.reserve(nfds);
            for (int fd = 0; fd < nfds; fd++)
            {
                short events = 0;
                if (readfds && FD_ISSET(fd, readfds))
                    events |= pollin | pollrdhup;
                if (writefds && FD_ISSET(fd, writefds))
                    events |= pollout;
                if (exceptfds && FD_ISSET(fd, exceptfds))
                    events |= pollpri;
                if (!events)
                    continue;

                pfds.emplace_back(fd, events, 0);
            }

            const auto timer = chrono::main_timer();

            std::uint64_t start_ns = 0;
            if (update_timeout && timeout)
                start_ns = timer->ns();

            const auto ret = ppoll(pfds, timeout, sigmask);

            if (update_timeout && timeout && ret >= 0)
            {
                const auto elapsed = timer->ns() - start_ns;
                const auto requested = timeout->to_ns();

                if (elapsed >= requested)
                {
                    timeout->tv_sec = 0;
                    timeout->tv_nsec = 0;
                }
                else
                {
                    const auto left = requested - elapsed;
                    timeout->tv_sec = static_cast<time_t>(left / 1'000'000'000ul);
                    timeout->tv_nsec = static_cast<long>(left % 1'000'000'000ul);
                }
            }

            if (ret < 0)
                return ret;

            if (readfds)
                FD_ZERO(readfds);
            if (writefds)
                FD_ZERO(writefds);
            if (exceptfds)
                FD_ZERO(exceptfds);

            int ready = 0;
            for (auto &pfd : pfds)
            {
                bool any = false;
                if (readfds && (pfd.revents & (pollin  | pollhup | pollrdhup)))
                {
                    FD_SET(pfd.fd, readfds);
                    any = true;
                }
                if (writefds && (pfd.revents & (pollout | pollhup)))
                {
                    FD_SET(pfd.fd, writefds);
                    any = true;
                }
                if (exceptfds && (pfd.revents & (pollpri | pollerr)))
                {
                    FD_SET(pfd.fd, exceptfds);
                    any = true;
                }
                if (any)
                    ready++;
            }
            return ready;
        }

        int pselect(int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds, timespec *timeout, bool update_timeout, const sigset_t __user *sigmask)
        {
            fd_set kreadfds, kwritefds, kexceptfds;
            sigset_t ksigmask;

            if (readfds && !lib::copy_from_user(&kreadfds, readfds, sizeof(fd_set)))
                    return -EFAULT;
            if (writefds && !lib::copy_from_user(&kwritefds, writefds, sizeof(fd_set)))
                    return -EFAULT;
            if (exceptfds && !lib::copy_from_user(&kexceptfds, exceptfds, sizeof(fd_set)))
                    return -EFAULT;
            if (sigmask && !lib::copy_from_user(&ksigmask, sigmask, sizeof(sigset_t)))
                    return -EFAULT;

            const auto ret = pselect(nfds,
                readfds ? &kreadfds : nullptr,
                writefds ? &kwritefds : nullptr,
                exceptfds ? &kexceptfds : nullptr,
                timeout, update_timeout,
                sigmask ? &ksigmask : nullptr
            );

            if (ret >= 0)
            {
                if (readfds && !lib::copy_to_user(readfds, &kreadfds, sizeof(fd_set)))
                    return -EFAULT;
                if (writefds && !lib::copy_to_user(writefds, &kwritefds, sizeof(fd_set)))
                        return -EFAULT;
                if (exceptfds && !lib::copy_to_user(exceptfds, &kexceptfds, sizeof(fd_set)))
                        return -EFAULT;
            }

            return ret;
        }
    } // namespace

    int ppoll(
        pollfd __user *fds, nfds_t nfds, timespec __user *timeout,
        sigset_t __user *sigmask
    )
    {
        if (fds == nullptr)
            return -EFAULT;

        std::vector<pollfd> kfds;
        for (nfds_t i = 0; i < nfds; i++)
        {
            auto &kfd = kfds.emplace_back();
            if (!lib::copy_from_user(&kfd, &fds[i], sizeof(pollfd)))
                return -EFAULT;
        }

        timespec ktimeout;
        if (timeout != nullptr)
        {
            if (!lib::copy_from_user(&ktimeout, timeout, sizeof(timespec)))
                return -EFAULT;
        }

        sigset_t ksigmask;
        if (sigmask != nullptr)
        {
            if (!lib::copy_from_user(&ksigmask, sigmask, sizeof(sigset_t)))
                return -EFAULT;
        }

        const auto ret = ppoll(
            kfds,
            timeout ? &ktimeout : nullptr,
            sigmask ? &ksigmask : nullptr
        );

        for (nfds_t i = 0; i < nfds; i++)
        {
            if (!lib::copy_to_user(&fds[i], &kfds[i], sizeof(pollfd)))
                return -EFAULT;
        }

        if (ret >= 0 && timeout != nullptr)
        {
            if (!lib::copy_to_user(timeout, &ktimeout, sizeof(timespec)))
                return -EFAULT;
        }

        return ret;
    }

    int poll(pollfd __user *fds, nfds_t nfds, int timeout)
    {
        if (fds == nullptr)
            return -EFAULT;

        std::vector<pollfd> kfds;
        for (nfds_t i = 0; i < nfds; i++)
        {
            auto &kfd = kfds.emplace_back();
            if (!lib::copy_from_user(&kfd, &fds[i], sizeof(pollfd)))
                return -EFAULT;
        }

        int ret;
        if (timeout >= 0)
        {
            timespec ts {
                timeout / 1000,
                (timeout % 1000) * 1'000'000L
            };
            ret = ppoll(kfds, &ts, nullptr);
        }
        else ret = ppoll(kfds, nullptr, nullptr);

        for (nfds_t i = 0; i < nfds; i++)
        {
            if (!lib::copy_to_user(&fds[i], &kfds[i], sizeof(pollfd)))
                return -EFAULT;
        }

        return ret;
    }

    int select(
        int nfds, fd_set __user *readfds, fd_set __user *writefds,
        fd_set __user *exceptfds, timeval __user *timeout
    )
    {
        timespec ktimeout;
        if (timeout != nullptr)
        {
            timeval ktimeval;
            if (!lib::copy_from_user(&ktimeval, timeout, sizeof(timeval)))
                return -EFAULT;
            ktimeout = timespec::from_timeval(ktimeval);
        }

        const auto ret = pselect(
            nfds, readfds, writefds, exceptfds,
            timeout ? &ktimeout : nullptr,
            (timeout != nullptr), nullptr
        );

        if (ret >= 0 && timeout != nullptr)
        {
            const auto ktimeval = ktimeout.to_timeval();
            if (!lib::copy_to_user(timeout, &ktimeval, sizeof(timeval)))
                return -EFAULT;
        }

        return ret;
    }

    int pselect6(
        int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds,
        const timespec __user *timeout, const struct sigset_t __user *sigmask
    )
    {
        timespec ktimeout;
        if (timeout != nullptr)
        {
            if (!lib::copy_from_user(&ktimeout, timeout, sizeof(timespec)))
                return -EFAULT;
        }

        return pselect(
            nfds, readfds, writefds, exceptfds,
            timeout ? &ktimeout : nullptr,
            false, sigmask
        );
    }
} // namespace syscall::vfs
