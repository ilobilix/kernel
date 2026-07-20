// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        constexpr int in_cloexec = o_cloexec;
        constexpr int in_nonblock = o_nonblock;

        struct instance_t
        {
            std::atomic<std::int32_t> next_wd { 1 };
        };

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
                // TODO
                lib::unused(file, offset, buffer);
                return std::unexpected { lib::err::try_again };
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
                // TODO
                lib::unused(file, pt);
                return 0;
            }

            lib::expect<int> ioctl(
                std::shared_ptr<file_t> file, std::uint64_t request,
                lib::uptr_or_addr argp
            ) override
            {
                // TODO
                lib::unused(file, request, argp);
                return std::unexpected { lib::err::inappropriate_ioctl };
            }
        };

        lib::expect<std::shared_ptr<instance_t>> get_instance(int fd)
        {
            const auto proc = sched::current_process();
            const auto fdesc_res = detail::get_fd(proc, fd);
            if (!fdesc_res)
                return std::unexpected { fdesc_res.error() };

            const auto &file = (*fdesc_res)->file;
            if (file->ops != ops_t::singleton())
                return std::unexpected { lib::err::invalid_argument };

            return std::static_pointer_cast<instance_t>(file->private_data);
        }
    } // namespace

    int inotify_init1(int flags)
    {
        if (flags & ~(in_cloexec | in_nonblock))
            return -EINVAL;

        auto ret = create_anon_fd({
            .name = "<[INOTIFY]>",
            .ops = ops_t::singleton(),
            .file_private_data = std::make_shared<instance_t>(),
            .inode_private_data = nullptr,
            .st_mode = std::to_underlying(stat::s_ifreg) | s_irusr,
            .flags = flags | o_rdonly,
            .skip_open = true,
            .inode = nullptr
        });
        if (!ret)
            return -lib::map_error(ret.error());

        return ret->first;
    }

    int inotify_init()
    {
        return inotify_init1(0);
    }

    int inotify_add_watch(int fd, const char __user *pathname, std::uint32_t mask)
    {
        // TODO: actually watch
        lib::unused(mask);

        const auto inst = get_instance(fd);
        if (!inst)
            return -lib::map_error(inst.error());

        auto path = detail::get_path(pathname);
        if (!path)
            return -lib::map_error(path.error());

        const auto proc = sched::current_process();
        if (const auto res = ::vfs::resolve(proc->vfs->cwd, path->str()); !res)
            return -lib::map_error(res.error());

        return (*inst)->next_wd.fetch_add(1, std::memory_order_relaxed);
    }

    int inotify_rm_watch(int fd, std::int32_t wd)
    {
        // TODO
        const auto inst = get_instance(fd);
        if (!inst)
            return -lib::map_error(inst.error());

        if (wd <= 0 || wd >= (*inst)->next_wd.load(std::memory_order_relaxed))
            return -EINVAL;
        return 0;
    }
} // namespace syscall::vfs
