// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

namespace syscall::vfs
{
    using namespace ::vfs;

    struct [[gnu::packed]] signalfd_siginfo
    {
        std::uint32_t ssi_signo;
        std::int32_t ssi_errno;
        std::int32_t ssi_code;
        std::uint32_t ssi_pid;
        std::uint32_t ssi_uid;
        std::int32_t ssi_fd;
        std::uint32_t ssi_tid;
        std::uint32_t ssi_band;
        std::uint32_t ssi_overrun;
        std::uint32_t ssi_trapno;
        std::int32_t ssi_status;
        std::int32_t ssi_int;
        std::uint64_t ssi_ptr;
        std::uint64_t ssi_utime;
        std::uint64_t ssi_stime;
        std::uint64_t ssi_addr;
        std::uint16_t ssi_addr_lsb;
        std::uint8_t pad[46];
    };
    static_assert(sizeof(signalfd_siginfo) == 128);

    namespace
    {
        constexpr int sfd_cloexec = o_cloexec;
        constexpr int sfd_nonblock = o_nonblock;

        struct data_t
        {
            lib::locker<
                sched::sigset_t,
                lib::spinlock
            > mask;
            sched::wait_queue_t bell;

            sched::signal_waiter_t waiter;
            std::weak_ptr<sched::process_t> wproc;
            bool registered = false;

            explicit data_t(const sched::sigset_t &mask)
                : mask { mask }, bell { } { }

            ~data_t()
            {
                if (!registered)
                    return;
                if (auto proc = wproc.lock())
                    sched::remove_signal_waiter(proc.get(), waiter);
            }

            void register_with(
                const std::shared_ptr<sched::process_t> &proc,
                const sched::sigset_t &interest
            )
            {
                waiter.interest = interest;
                waiter.wake = [this] { bell.wake_all(); };
                wproc = proc;
                sched::add_signal_waiter(proc.get(), waiter);
                registered = true;
            }
        };

        signalfd_siginfo to_siginfo(const sched::siginfo_t &info)
        {
            signalfd_siginfo ss { };
            ss.ssi_signo = static_cast<std::uint32_t>(info.signo);
            ss.ssi_errno = info.err;
            ss.ssi_code = info.code;
            ss.ssi_pid = static_cast<std::uint32_t>(info.pid);
            ss.ssi_uid = static_cast<std::uint32_t>(info.uid);
            ss.ssi_status = info.status;
            ss.ssi_int = static_cast<std::int32_t>(info.value);
            ss.ssi_ptr = info.value;
            ss.ssi_addr = info.addr;
            return ss;
        }

        struct ops_t : ::vfs::ops_t
        {
            static std::shared_ptr<ops_t> singleton()
            {
                static auto instance = std::make_shared<ops_t>();
                return instance;
            }

            bool seekable() const override { return false; }

            lib::expect<void> open(std::shared_ptr<vfs::file_t> file, int flags, pid_t pid) override
            {
                lib::unused(file, flags, pid);
                return std::unexpected { lib::err::invalid_device_or_address };
            }

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);

                if (buffer.size() < sizeof(signalfd_siginfo))
                    return std::unexpected { lib::err::invalid_argument };

                const auto max = buffer.size() / sizeof(signalfd_siginfo);

                const bool nonblock = file->flags & sfd_nonblock;
                auto data = std::static_pointer_cast<data_t>(file->private_data);
                auto proc = sched::current_process();

                const auto mask = *data->mask.lock();
                const auto drain = [&](std::size_t &count) -> lib::expect<void>
                {
                    while (count < max)
                    {
                        const auto info = sched::dequeue_signal(proc, mask);
                        if (!info)
                            break;

                        const auto ss = to_siginfo(*info);
                        if (!buffer.subspan(count * sizeof(ss), sizeof(ss))
                                .copy_from(std::as_bytes(std::span { &ss, 1 })))
                            return std::unexpected { lib::err::invalid_address };

                        count++;
                    }
                    return { };
                };

                while (true)
                {
                    std::size_t count = 0;
                    if (const auto res = drain(count); !res)
                        return std::unexpected { res.error() };

                    if (count > 0)
                        return count * sizeof(signalfd_siginfo);

                    if (nonblock)
                        return std::unexpected { lib::err::try_again };

                    const auto res = data->bell.wait();
                    if (res.interrupted || res.killed)
                    {
                        std::size_t after = 0;
                        if (const auto ret = drain(after); !ret)
                            return std::unexpected { ret.error() };
                        if (after > 0)
                            return after * sizeof(signalfd_siginfo);

                        return std::unexpected { lib::err::interrupted };
                    }
                }
            }

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(file, offset, buffer);
                return std::unexpected { lib::err::invalid_argument };
            }

            lib::expect<std::uint16_t> poll(
                std::shared_ptr<vfs::file_t> file, vfs::poll_table_t *pt
            ) override
            {
                auto data = std::static_pointer_cast<data_t>(file->private_data);
                if (pt)
                    pt->add(data->bell);

                const auto mask = *data->mask.lock();
                auto proc = sched::current_process();

                const std::unique_lock _ { proc->sigqueue.lock };
                if ((proc->sigqueue.pending & mask).any())
                    return pollin;

                return 0;
            }
        };
    } // namespace

    int signalfd4(int fd, sigset_t __user *mask, std::size_t sizemask, int flags)
    {
        if (flags & ~(sfd_cloexec | sfd_nonblock))
            return -EINVAL;

        if (sizemask != sizeof(sched::sigset_t))
            return -EINVAL;

        sched::sigset_t kmask;
        if (!mask || !lib::copy_from_user(&kmask, mask, sizeof(kmask)))
            return -EFAULT;
        kmask &= ~sched::sigmask_uncatchable;

        if (fd != -1)
        {
            auto desc = sched::current_process()->fdt->get(fd);
            if (!desc || !desc->file)
                return -EBADF;
            if (desc->file->ops.get() != ops_t::singleton().get())
                return -EINVAL;

            auto data = std::static_pointer_cast<data_t>(desc->file->private_data);
            *data->mask.lock() = kmask;
            if (data->registered)
            {
                if (auto proc = data->wproc.lock())
                    sched::update_signal_waiter(proc.get(), data->waiter, kmask);
            }
            return fd;
        }

        auto data = std::make_shared<data_t>(kmask);
        auto ret = create_anon_fd({
            .name = "<[SIGNALFD]>",
            .ops = ops_t::singleton(),
            .file_private_data = data,
            .inode_private_data = nullptr,
            .st_mode = std::to_underlying(stat::s_ifreg) | s_irusr | s_iwusr,
            .flags = flags | o_rdwr,
            .skip_open = true,
            .inode = nullptr
        });
        if (!ret)
            return -lib::map_error(ret.error());

        if (auto proc = sched::get_process(sched::current_process()->pid))
            data->register_with(proc, kmask);

        return ret->first;
    }

    int signalfd(int fd, sigset_t __user *mask, std::size_t sizemask)
    {
        return signalfd4(fd, mask, sizemask, 0);
    }
} // namespace syscall::vfs
