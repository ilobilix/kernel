// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        constexpr int pidfd_nonblock = o_nonblock;
        // constexpr int pidfd_thread = o_excl; // TODO
        constexpr int supported_flags = pidfd_nonblock;

        // TODO
        // constexpr std::uint32_t signal_thread = (1 << 0);
        // constexpr std::uint32_t signal_thread_group = (1 << 1);
        // constexpr std::uint32_t signal_process_group = (1 << 2);
        constexpr std::uint32_t supported_signal_flags = 0;

        struct ops_t : ::vfs::ops_t
        {
            static std::shared_ptr<ops_t> singleton()
            {
                static auto instance = std::make_shared<ops_t>();
                return instance;
            }

            bool seekable() const override { return false; }

            lib::expect<void> open(
                const std::shared_ptr<vfs::file_t> &file, int flags, pid_t pid
            ) override
            {
                lib::unused(file, flags, pid);
                return std::unexpected { lib::err::invalid_device_or_address };
            }

            lib::expect<std::size_t> read(
                const std::shared_ptr<vfs::file_t> &file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(file, offset, buffer);
                return std::unexpected { lib::err::invalid_argument };
            }

            lib::expect<std::size_t> write(
                const std::shared_ptr<vfs::file_t> &file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(file, offset, buffer);
                return std::unexpected { lib::err::invalid_argument };
            }

            lib::expect<std::uint16_t> poll(
                const std::shared_ptr<vfs::file_t> &file, vfs::poll_table_t *pt
            ) override
            {
                auto proc = std::static_pointer_cast<sched::process_t>(file->private_data);
                if (pt)
                    pt->add(proc->exited);

                return proc->is_zombie ? pollin : 0; // TODO: pollhup when reaped
            }
        };

        lib::expect<std::shared_ptr<sched::process_t>> get_process(int fd)
        {
            auto *proc = sched::current_process();
            const auto fdesc_res = detail::get_fd(proc, fd);
            if (!fdesc_res)
                return std::unexpected { fdesc_res.error() };

            const auto &file = (*fdesc_res)->file;
            if (file->ops != ops_t::singleton())
                return std::unexpected { lib::err::invalid_fd };

            return std::static_pointer_cast<sched::process_t>(file->private_data);
        }

        bool can_access(const sched::process_t *target)
        {
            // TODO: PTRACE_MODE_ATTACH_REALCREDS check
            // const auto &cred = sched::current_process()->cred;
            // return sched::capable(cred, sched::cap_t::sys_ptrace);
            lib::unused(target);
            return true;
        }
    } // namespace

    namespace detail
    {
        lib::expect<int> make_pidfd(const std::shared_ptr<sched::process_t> &proc, int flags)
        {
            if (flags & ~supported_flags)
                return std::unexpected { lib::err::invalid_argument };

            auto ret = create_anon_fd({
                .name = "[pidfd]",
                .ops = ops_t::singleton(),
                .file_private_data = proc,
                .inode_private_data = nullptr,
                .st_mode = std::to_underlying(stat::s_ifreg) | s_irusr | s_iwusr,
                .flags = static_cast<int>(flags | o_cloexec | o_rdwr),
                .skip_open = true,
                .inode = nullptr
            });
            if (!ret)
                return std::unexpected { ret.error() };
            return ret->first;
        }

        lib::expect<std::shared_ptr<sched::process_t>> pidfd_to_process(int fd)
        {
            return get_process(fd);
        }
    } // namespace detail

    int pidfd_open(pid_t pid, std::uint32_t flags)
    {
        if (flags & ~supported_flags)
            return -EINVAL;

        if (pid <= 0)
            return -EINVAL;

        auto proc = sched::get_process(pid);
        if (!proc)
            return -ESRCH;

        auto ret = detail::make_pidfd(proc, flags);
        if (!ret)
            return -lib::map_error(ret.error());
        return *ret;
    }

    int pidfd_getfd(int pidfd, int fd, std::uint32_t flags)
    {
        if (flags != 0)
            return -EINVAL;

        if (fd < 0)
            return -EBADF;

        auto target = get_process(pidfd);
        if (!target)
            return -lib::map_error(target.error());

        if ((*target)->is_zombie)
            return -ESRCH;

        if (!can_access(target->get()))
            return -EPERM;

        const auto tfdt = (*target)->fdt;
        if (!tfdt)
            return -ESRCH;

        auto fdesc = tfdt->get(fd);
        if (!fdesc)
            return -EBADF;

        const auto *caller = sched::current_process();
        auto newfdesc = std::make_shared<filedesc>(fdesc->file, true);

        const auto ret = caller->fdt->alloc(
            std::move(newfdesc), 0, false,
            caller->rlimits->get(sched::rlimit_nofile).cur
        );
        if (!ret)
            return -lib::map_error(ret.error());

        return *ret;
    }

    int pidfd_send_signal(
        int pidfd, int sig, sched::user_siginfo_t __user *info, std::uint32_t flags
    )
    {
        using namespace sched;

        if (flags & ~supported_signal_flags)
            return -EINVAL;

        if (sig < 0 || sig > nsig)
            return -EINVAL;

        const auto target = get_process(pidfd);
        if (!target)
            return -lib::map_error(target.error());

        if ((*target)->is_zombie)
            return -ESRCH;

        if (!check_kill(sig, target->get()))
            return -EPERM;

        if (sig == 0)
            return 0;

        const auto *caller = current_process();
        siginfo_t kinfo {
            .signo = sig,
            .code = si_user,
            .err = 0,
            .pid = caller->pid,
            .uid = caller->cred->ruid,
            .status = 0,
            .addr = 0,
            .value = 0,
        };

        if (info)
        {
            user_siginfo_t uinfo { };
            if (!lib::copy_from_user(&uinfo, info, sizeof(uinfo)))
                return -EFAULT;

            if (uinfo.signo != sig)
                return -EINVAL;

            if ((*target)->pid != caller->pid && (uinfo.code >= 0 || uinfo.code == si_tkill))
                return -EPERM;

            kinfo.code = uinfo.code;
            kinfo.err = uinfo.err;
            kinfo.pid = uinfo.rt.pid;
            kinfo.uid = uinfo.rt.uid;
            kinfo.value = uinfo.rt.value;
        }

        return send_signal(target->get(), kinfo) ? 0 : -ESRCH;
    }
} // namespace syscall::vfs
