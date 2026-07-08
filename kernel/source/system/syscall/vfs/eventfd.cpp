// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        constexpr auto ev_max = std::numeric_limits<std::uint64_t>::max() - 1;
        constexpr int efd_semaphore = (1 << 0);
        constexpr int efd_cloexec = o_cloexec;
        constexpr int efd_nonblock = o_nonblock;

        struct data_t
        {
            lib::locker<
                std::uint64_t,
                lib::spinlock
            > counter;
            sched::wait_queue_t bell;
            bool semaphore;

            data_t(std::uint64_t initial, bool semaphore)
                : counter { initial }, bell { }, semaphore { semaphore } { }
        };

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
                lib::unused(offset);
                if (buffer.size() < sizeof(std::uint64_t))
                    return std::unexpected { lib::err::invalid_argument };

                const bool nonblock = file->flags & efd_nonblock;
                auto data = std::static_pointer_cast<data_t>(file->private_data);

                std::uint64_t ret;
                {
                    auto locked = data->counter.lock();
                    again:
                    if (*locked != 0)
                    {
                        if (!data->semaphore)
                        {
                            ret = *locked;
                            *locked = 0;
                        }
                        else
                        {
                            ret = 1;
                            (*locked)--;
                        }
                    }
                    else
                    {
                        if (nonblock)
                            return std::unexpected { lib::err::try_again };

                        while (true)
                        {
                            const auto gen = data->bell.snapshot_gen();
                            locked.unlock();
                            {
                                const auto res = data->bell.wait_prepared(gen);
                                if (res.interrupted || res.killed)
                                    return std::unexpected { lib::err::interrupted };
                            }
                            locked.lock();
                            if (*locked != 0)
                                goto again;
                        }
                    }
                }

                if (!buffer.copy_from(std::as_bytes(std::span { &ret, 1 })))
                    return std::unexpected { lib::err::invalid_address };

                data->bell.wake_all();
                return sizeof(ret);
            }

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);

                if (buffer.size() < sizeof(std::uint64_t))
                    return std::unexpected { lib::err::invalid_argument };

                std::uint64_t val;
                if (!buffer.copy_to(std::as_writable_bytes(std::span { &val, 1 })))
                    return std::unexpected { lib::err::invalid_address };

                if (val > ev_max)
                    return std::unexpected { lib::err::invalid_argument };

                const bool nonblock = file->flags & efd_nonblock;
                auto data = std::static_pointer_cast<data_t>(file->private_data);

                while (true)
                {
                    bool blocked = false;
                    std::size_t gen = 0;
                    {
                        auto locked = data->counter.lock();
                        if (val > ev_max - *locked)
                        {
                            gen = data->bell.snapshot_gen();
                            blocked = true;
                        }
                        else *locked += val;
                    }

                    if (!blocked)
                    {
                        data->bell.wake_all();
                        return sizeof(val);
                    }

                    if (nonblock)
                        return std::unexpected { lib::err::try_again };

                    const auto res = data->bell.wait_prepared(gen);
                    if (res.interrupted || res.killed)
                        return std::unexpected { lib::err::interrupted };
                }
            }

            lib::expect<std::uint16_t> poll(
                std::shared_ptr<vfs::file> file, vfs::poll_table *pt
            ) override
            {
                auto data = std::static_pointer_cast<data_t>(file->private_data);
                if (pt)
                    pt->add(data->bell);

                auto locked = data->counter.lock();
                std::uint16_t mask = 0;

                if (*locked > 0)
                    mask |= pollin;
                if (*locked < ev_max)
                    mask |= pollout;

                return mask;
            }
        };
    } // namespace

    int eventfd2(unsigned int count, int flags)
    {
        if (flags & ~(efd_semaphore | efd_cloexec | efd_nonblock))
            return -EINVAL;

        auto ret = create_anon_fd({
            .name = "<[EVENTFD]>",
            .ops = ops::singleton(),
            .file_private_data = std::make_shared<data_t>(count, flags & efd_semaphore),
            .inode_private_data = nullptr,
            .st_mode = std::to_underlying(stat::s_ifreg) | s_irusr | s_iwusr,
            .flags = (flags & ~efd_semaphore) | o_rdwr,
            .skip_open = true,
            .inode = nullptr
        });
        if (!ret)
            return -lib::map_error(ret.error());

        return ret->first;
    }

    int eventfd(unsigned int count)
    {
        return eventfd2(count, 0);
    }
} // namespace syscall::vfs
