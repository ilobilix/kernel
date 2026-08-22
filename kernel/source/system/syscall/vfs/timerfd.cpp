// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.chrono;
import magic_enum;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        constexpr int tfd_timer_abstime = (1 << 0);
        constexpr int tfd_timer_cancel_on_set = (1 << 1);
        constexpr int tfd_cloexec = o_cloexec;
        constexpr int tfd_nonblock = o_nonblock;
        constexpr int tfd_ioc_set_ticks = 0x40085400;

        struct instance_t : sched::timer_t
        {
            std::uint64_t ticks = 0;
            bool might_cancel = false; // cancel_on_set
            bool cancelled = false;

            sched::wait_queue_t bell;

            instance_t(clockid_t clockid)
                : sched::timer_t { static_cast<chrono::type>(clockid) } { }

            void expired(std::uint64_t missed) override { ticks += missed; }
            void notify() override { bell.wake_all(); }

            void rearmed() override
            {
                ticks = 0;
                cancelled = false;
            }
        };

        lib::locker<
            std::vector<
                std::weak_ptr<instance_t>
            >, lib::spinlock
        > cancel_list;

        void cleanup(std::vector<std::weak_ptr<instance_t>> &list)
        {
            for (const auto *it = list.begin(); it != list.end(); )
            {
                if (it->expired())
                    it = list.erase(it);
                else
                    it++;
            }
        }

        void clock_was_set()
        {
            std::vector<std::shared_ptr<instance_t>> live;
            {
                const auto locked = cancel_list.lock();
                cleanup(*locked);

                for (const auto &weak : *locked)
                {
                    if (auto data = weak.lock())
                        live.push_back(std::move(data));
                }
            }

            for (const auto &data : live)
            {
                bool notify = false;
                {
                    const std::unique_lock _ { data->lock };
                    if (data->might_cancel && !data->cancelled)
                    {
                        data->cancelled = true;
                        data->ticks++;
                        notify = true;
                    }
                }
                if (notify)
                    data->bell.wake_all();
            }
        }

        void watch_clock(const std::shared_ptr<instance_t> &data)
        {
            [[maybe_unused]]
            static const bool registered = [] {
                static chrono::clock_set_hook hook {
                    clock_was_set, nullptr
                };
                chrono::on_clock_set(hook);
                return true;
            } ();

            const auto locked = cancel_list.lock();
            cleanup(*locked);

            const auto present = std::ranges::any_of(
                *locked, [&data](const std::weak_ptr<instance_t> &weak) {
                    return weak.lock() == data;
                }
            );

            if (!present)
                locked->push_back(data);
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
                if (buffer.size() < sizeof(std::uint64_t))
                    return std::unexpected { lib::err::invalid_argument };

                const bool nonblock = file->flags & tfd_nonblock;
                auto data = std::static_pointer_cast<instance_t>(file->private_data);

                std::uint64_t ret;
                {
                    std::unique_lock locked { data->lock };
                    while (data->ticks == 0)
                    {
                        if (nonblock)
                            return std::unexpected { lib::err::try_again };

                        const auto gen = data->bell.snapshot_gen();
                        locked.unlock();
                        {
                            const auto res = data->bell.wait_prepared(gen);
                            if (res.interrupted || res.killed)
                                return std::unexpected { lib::err::interrupted };
                        }
                        locked.lock();
                    }

                    if (std::exchange(data->cancelled, false))
                    {
                        data->ticks = 0;
                        return std::unexpected { lib::err::cancelled };
                    }
                    ret = std::exchange(data->ticks, 0);
                }

                if (!buffer.copy_from(std::as_bytes(std::span { &ret, 1 })))
                    return std::unexpected { lib::err::invalid_address };

                return sizeof(ret);
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
                auto data = std::static_pointer_cast<instance_t>(file->private_data);
                if (pt)
                    pt->add(data->bell);

                const std::unique_lock _ { data->lock };
                return data->ticks > 0 ? pollin : 0;
            }

            lib::expect<int> ioctl(
                std::shared_ptr<vfs::file_t> file, std::uint64_t request,
                lib::uptr_or_addr argp
            ) override
            {
                if (request != tfd_ioc_set_ticks)
                    return std::unexpected { lib::err::inappropriate_ioctl };

                std::uint64_t ticks;
                if (!argp.read(ticks))
                    return std::unexpected { lib::err::invalid_address };

                if (ticks == 0)
                    return std::unexpected { lib::err::invalid_argument };

                auto data = std::static_pointer_cast<instance_t>(file->private_data);
                {
                    const std::unique_lock _ { data->lock };
                    if (std::exchange(data->cancelled, false))
                        return std::unexpected { lib::err::cancelled };

                    data->ticks = ticks;
                }

                data->bell.wake_all();
                return 0;
            }
        };

        lib::expect<std::shared_ptr<instance_t>> get_instance(int fd)
        {
            auto *proc = sched::current_process();
            const auto fdesc_res = detail::get_fd(proc, fd);
            if (!fdesc_res)
                return std::unexpected { fdesc_res.error() };

            const auto &file = (*fdesc_res)->file;
            if (file->ops != ops_t::singleton())
                return std::unexpected { lib::err::invalid_argument };

            return std::static_pointer_cast<instance_t>(file->private_data);
        }
    } // namespace

    int timerfd_create(int clockid, int flags)
    {
        if (flags & ~(tfd_cloexec | tfd_nonblock))
            return -EINVAL;

        if (!magic_enum::enum_contains(static_cast<chrono::type>(clockid)))
            return -EINVAL;

        // TODO
        if (clockid == chrono::boottime)
            clockid = chrono::monotonic;

        auto ret = create_anon_fd({
            .name = "[timerfd]",
            .ops = ops_t::singleton(),
            .file_private_data = std::make_shared<instance_t>(clockid),
            .inode_private_data = nullptr,
            .st_mode = std::to_underlying(stat::s_ifreg) | s_irusr | s_iwusr,
            .flags = flags | o_rdwr,
            .skip_open = true,
            .inode = nullptr
        });
        if (!ret)
            return -lib::map_error(ret.error());
        return ret->first;
    }

    int timerfd_settime(
        int fd, int flags,
        const itimerspec __user *ntmr,
        itimerspec __user *otmr
    )
    {
        if (flags & ~(tfd_timer_abstime | tfd_timer_cancel_on_set))
            return -EINVAL;

        itimerspec knew;
        if (!lib::copy_from_user(&knew, ntmr, sizeof(knew)))
            return -EFAULT;

        if (!knew.valid())
            return -EINVAL;

        auto res = get_instance(fd);
        if (!res)
            return -lib::map_error(res.error());
        const auto &data = *res;

        const auto clockid = data->clockid;
        const bool arming = knew.value.tv_sec != 0 || knew.value.tv_nsec != 0;

        const bool might_cancel = arming && clockid == chrono::realtime &&
            (flags & tfd_timer_abstime) && (flags & tfd_timer_cancel_on_set);

        {
            const std::unique_lock _ { data->lock };
            data->might_cancel = might_cancel;
        }

        if (might_cancel)
            watch_clock(data);

        const auto kold = data->settime(flags & tfd_timer_abstime, knew).to_itimerspec();
        if (otmr && !lib::copy_to_user(otmr, &kold, sizeof(kold)))
            return -EFAULT;
        return 0;
    }

    int timerfd_gettime(int fd, itimerspec __user *otmr)
    {
        auto res = get_instance(fd);
        if (!res)
            return -lib::map_error(res.error());

        const auto kold = (*res)->query().to_itimerspec();
        if (!lib::copy_to_user(otmr, &kold, sizeof(kold)))
            return -EFAULT;
        return 0;
    }
} // namespace syscall::vfs
