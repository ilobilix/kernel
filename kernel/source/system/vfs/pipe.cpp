// Copyright (C) 2024-2026  ilobilo

module system.vfs.pipe;

import system.sched;
import system.sched.wait_queue;
import system.sched.mutex;
import std;

namespace vfs::pipe
{
    namespace
    {
        constexpr std::size_t pipe_buf = 0x1000;
        constexpr std::size_t default_capacity = 0x10000;
        constexpr std::size_t max_capacity = 0x100000;

        std::size_t round_capacity(std::size_t size)
        {
            return lib::next_pow2(std::clamp(size, pipe_buf, max_capacity));
        }

        struct data
        {
            sched::mutex lock;

            lib::membuffer storage;
            std::size_t capacity, head, tail, buffered;

            std::size_t readers;
            std::size_t writers;

            bool anon;

            sched::wait_queue_t read_wait;
            sched::wait_queue_t write_wait;
            sched::wait_queue_t open_wait;

            explicit data(std::size_t cap = default_capacity)
                : storage { cap }, capacity { cap }, head { 0 }, tail { 0 },
                  buffered { 0 }, readers { 0 }, writers { 0 }, anon { false } { }
        };

        std::shared_ptr<data> get_or_create(const std::shared_ptr<vfs::inode> &inode)
        {
            const std::unique_lock _ { inode->lock };
            if (!inode->private_data)
                inode->private_data = std::make_shared<data>();
            return std::static_pointer_cast<data>(inode->private_data);
        }

        lib::expect<std::shared_ptr<data>> require_pipe(const std::shared_ptr<vfs::file> &file)
        {
            if (!is_pipe(file))
                return std::unexpected { lib::err::invalid_argument };
            return std::static_pointer_cast<data>(file->private_data);
        }

        std::size_t copy_in(
            data &pdata, lib::maybe_uspan<std::byte> src,
            std::size_t off, std::size_t len, bool &fault
        )
        {
            fault = false;
            const auto first = std::min(len, pdata.capacity - pdata.head);
            const auto rest = len - first;

            if (!src.subspan(off, first).copy_to({ pdata.storage.data() + pdata.head, first }))
            {
                fault = true;
                return 0;
            }

            pdata.head = (pdata.head + first) & (pdata.capacity - 1);
            pdata.buffered += first;

            if (rest == 0)
                return first;

            if (!src.subspan(off + first, rest).copy_to({ pdata.storage.data(), rest }))
            {
                fault = true;
                return first;
            }

            pdata.head = (pdata.head + rest) & (pdata.capacity - 1);
            pdata.buffered += rest;
            return len;
        }

        std::size_t copy_out(
            data &pdata, lib::maybe_uspan<std::byte> dst,
            std::size_t off, std::size_t len, bool &fault
        )
        {
            fault = false;
            const auto first = std::min(len, pdata.capacity - pdata.tail);
            const auto rest = len - first;

            if (!dst.subspan(off, first).copy_from({ pdata.storage.data() + pdata.tail, first }))
            {
                fault = true;
                return 0;
            }

            pdata.tail = (pdata.tail + first) & (pdata.capacity - 1);
            pdata.buffered -= first;

            if (rest == 0)
                return first;

            if (!dst.subspan(off + first, rest).copy_from({ pdata.storage.data(), rest }))
            {
                fault = true;
                return first;
            }

            pdata.tail = (pdata.tail + rest) & (pdata.capacity - 1);
            pdata.buffered -= rest;
            return len;
        }

        lib::expect<void> wait_for_writer(const std::shared_ptr<data> &pdata, bool nonblock)
        {
            while (true)
            {
                std::size_t gen;
                {
                    const std::unique_lock _ { pdata->lock };
                    if (pdata->writers > 0)
                        return { };
                    gen = pdata->open_wait.snapshot_gen();
                }
                if (nonblock)
                    return { };

                if (pdata->open_wait.wait_prepared(gen))
                {
                    const std::unique_lock _ { pdata->lock };
                    pdata->readers--;
                    return std::unexpected { lib::err::interrupted };
                }
            }
        }

        lib::expect<void> wait_for_reader(const std::shared_ptr<data> &pdata, bool nonblock)
        {
            while (true)
            {
                std::size_t gen;
                {
                    const std::unique_lock _ { pdata->lock };
                    if (pdata->readers > 0)
                        return { };

                    if (nonblock)
                    {
                        pdata->writers--;
                        return std::unexpected { lib::err::invalid_device_or_address };
                    }
                    gen = pdata->open_wait.snapshot_gen();
                }

                if (pdata->open_wait.wait_prepared(gen))
                {
                    const std::unique_lock _ { pdata->lock };
                    pdata->writers--;
                    return std::unexpected { lib::err::interrupted };
                }
            }
        }

        struct ops : vfs::ops
        {
            static std::shared_ptr<ops> singleton()
            {
                static auto instance = std::make_shared<ops>();
                return instance;
            }

            bool seekable() const override { return false; }

            lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags, pid_t pid) override
            {
                lib::unused(pid);
                lib::bug_on(!file || !file->path.dentry || !file->path.dentry->inode);

                const bool rd = is_read(flags);
                const bool wr = is_write(flags);

                if (!rd && !wr)
                    return std::unexpected { lib::err::invalid_flags };

                const auto pdata = get_or_create(file->path.dentry->inode);
                file->private_data = pdata;

                {
                    const std::unique_lock _ { pdata->lock };
                    if (rd)
                        pdata->readers++;
                    if (wr)
                        pdata->writers++;
                }
                pdata->open_wait.wake_all();

                if (pdata->anon || (rd && wr))
                    return { };

                const bool nonblock = (flags & o_nonblock) != 0;
                return rd ? wait_for_writer(pdata, nonblock) : wait_for_reader(pdata, nonblock);
            }

            lib::expect<void> close(vfs::file &file) override
            {
                lib::bug_on(!file.private_data);

                const auto pdata = std::static_pointer_cast<data>(file.private_data);

                const bool rd = is_read(file.flags);
                const bool wr = is_write(file.flags);

                bool wake_readers = false;
                bool wake_writers = false;
                {
                    const std::unique_lock _ { pdata->lock };
                    lib::bug_on(pdata->readers == 0 && pdata->writers == 0);
                    if (rd && pdata->readers-- == 1)
                        wake_writers = true;
                    if (wr && pdata->writers-- == 1)
                        wake_readers = true;
                }
                if (wake_readers)
                    pdata->read_wait.wake_all();
                if (wake_writers)
                    pdata->write_wait.wake_all();

                file.private_data.reset();
                return { };
            }

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer) override
            {
                lib::unused(offset);
                lib::bug_on(!file || !file->private_data);
                const auto pdata = std::static_pointer_cast<data>(file->private_data);

                if (buffer.size() == 0)
                    return 0uz;

                const bool nonblock = (file->flags & o_nonblock) != 0;

                while (true)
                {
                    std::size_t copied = 0;
                    bool fault = false;
                    bool no_writers = false;
                    std::size_t gen = 0;

                    {
                        const std::unique_lock _ { pdata->lock };

                        if (pdata->buffered > 0)
                        {
                            const auto want = std::min(buffer.size(), pdata->buffered);
                            copied = copy_out(*pdata, buffer, 0, want, fault);
                        }
                        else if (pdata->writers != 0)
                            gen = pdata->read_wait.snapshot_gen();
                        else
                            no_writers = true;
                    }

                    if (copied > 0)
                    {
                        pdata->write_wait.wake_all();
                        return copied;
                    }

                    if (fault)
                        return std::unexpected { lib::err::invalid_address };
                    if (no_writers)
                        return 0uz;

                    if (nonblock)
                        return std::unexpected { lib::err::try_again };

                    if (pdata->read_wait.wait_prepared(gen))
                        return std::unexpected { lib::err::interrupted };
                }
            }

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer) override
            {
                lib::unused(offset);
                lib::bug_on(!file || !file->private_data);
                const auto pdata = std::static_pointer_cast<data>(file->private_data);

                const std::size_t total = buffer.size();
                if (total == 0)
                    return 0uz;

                const bool nonblock = (file->flags & o_nonblock) != 0;
                const bool atomic = total <= pipe_buf;

                std::size_t written = 0;

                while (written < total)
                {
                    std::size_t chunk = 0;
                    bool fault = false;
                    bool no_readers = false;
                    std::size_t gen = 0;

                    {
                        const std::unique_lock _ { pdata->lock };

                        const auto avail = pdata->capacity - pdata->buffered;
                        const auto remaining = total - written;
                        const auto need = atomic ? remaining : 1uz;

                        if (pdata->readers != 0 && avail >= need)
                        {
                            const auto want = std::min(avail, remaining);
                            chunk = copy_in(*pdata, buffer, written, want, fault);
                        }
                        else if (pdata->readers != 0)
                            gen = pdata->write_wait.snapshot_gen();
                        else
                            no_readers = true;
                    }

                    if (chunk > 0)
                    {
                        written += chunk;
                        pdata->read_wait.wake_all();
                    }

                    if (no_readers)
                    {
                        if (written > 0)
                            return written;

                        const auto thread = sched::current_thread();
                        lib::bug_on(!thread);

                        const sched::siginfo_t info {
                            .signo = sched::sigpipe,
                            .code = sched::si_kernel,
                            .err = 0,
                            .pid = 0,
                            .uid = 0,
                            .status = 0,
                            .addr = 0,
                            .value = 0
                        };
                        sched::send_signal(thread, info);

                        return std::unexpected { lib::err::broken_pipe };
                    }

                    if (fault)
                    {
                        if (written > 0)
                            return written;
                        return std::unexpected { lib::err::invalid_address };
                    }

                    if (chunk > 0)
                        continue;

                    if (nonblock)
                    {
                        if (written > 0)
                            return written;
                        return std::unexpected { lib::err::try_again };
                    }

                    if (pdata->write_wait.wait_prepared(gen))
                    {
                        if (written > 0)
                            return written;
                        return std::unexpected { lib::err::interrupted };
                    }
                }

                return written;
            }

            lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
            {
                lib::unused(file, size);
                return std::unexpected { lib::err::invalid_argument };
            }

            lib::expect<std::uint16_t> poll(std::shared_ptr<vfs::file> file, vfs::poll_table *pt) override
            {
                lib::bug_on(!file || !file->private_data);
                const auto pdata = std::static_pointer_cast<data>(file->private_data);

                if (pt)
                {
                    pt->add(pdata->read_wait);
                    pt->add(pdata->write_wait);
                }

                std::uint16_t mask = 0;
                const bool reader = is_read(file->flags);
                const bool writer = is_write(file->flags);

                const std::unique_lock _ { pdata->lock };
                if (reader)
                {
                    if (pdata->buffered > 0)
                        mask |= pollin;
                    if (pdata->writers == 0)
                        mask |= pollhup;
                }

                if (writer)
                {
                    if (pdata->buffered < pdata->capacity)
                        mask |= pollout;
                    if (pdata->readers == 0)
                        mask |= pollerr;
                }

                return mask;
            }
        };
    } // namespace

    std::shared_ptr<vfs::ops> get_ops()
    {
        return ops::singleton();
    }

    void prep_anon(std::shared_ptr<vfs::inode> &inode)
    {
        const std::unique_lock _ { inode->lock };
        lib::bug_on(inode->private_data != nullptr);
        auto pdata = std::make_shared<data>();
        pdata->anon = true;
        inode->private_data = std::move(pdata);
    }

    bool is_pipe(const std::shared_ptr<vfs::file> &file)
    {
        if (!file || !file->path.dentry || !file->path.dentry->inode)
            return false;

        const auto fops = file->get_ops();
        if (!fops.has_value())
            return false;
        return fops->get() == ops::singleton().get();
    }

    lib::expect<std::size_t> get_size(std::shared_ptr<vfs::file> file)
    {
        const auto pdata = require_pipe(file);
        if (!pdata.has_value())
            return std::unexpected { pdata.error() };

        const std::unique_lock _ { (*pdata)->lock };
        return (*pdata)->capacity;
    }

    lib::expect<std::size_t> set_size(std::shared_ptr<vfs::file> file, std::size_t size)
    {
        if (size == 0)
            return std::unexpected { lib::err::invalid_argument };

        const auto pdata_res = require_pipe(file);
        if (!pdata_res.has_value())
            return std::unexpected { pdata_res.error() };

        const auto &pdata = *pdata_res;
        const auto new_cap = round_capacity(size);

        const std::unique_lock _ { pdata->lock };

        if (new_cap < pdata->buffered)
            return std::unexpected { lib::err::target_is_busy };

        if (new_cap == pdata->capacity)
            return pdata->capacity;

        lib::membuffer fresh { new_cap };
        if (pdata->buffered > 0)
        {
            const auto first = std::min(pdata->buffered, pdata->capacity - pdata->tail);
            const auto rest = pdata->buffered - first;
            std::memcpy(fresh.data(), pdata->storage.data() + pdata->tail, first);
            if (rest > 0)
                std::memcpy(fresh.data() + first, pdata->storage.data(), rest);
        }

        pdata->storage = std::move(fresh);
        pdata->capacity = new_cap;
        pdata->tail = 0;
        pdata->head = pdata->buffered;
        if (pdata->head == pdata->capacity)
            pdata->head = 0;

        pdata->write_wait.wake_all();
        return pdata->capacity;
    }
} // namespace vfs::pipe
