// Copyright (C) 2024-2026  ilobilo

module system.vfs.pipe;

import system.sched.wait_queue;
import system.vfs;
import lib;
import std;

namespace vfs::pipe
{
    struct data
    {
        static constexpr std::size_t buffer_size = 65536;
        // static constexpr std::size_t packet_size = 512;

        lib::rbmpmcd<char, buffer_size> buffer;
        // lib::rbmpmcd<std::string, (buffer_size / packet_size)> packets;

        std::atomic_size_t readers = 0;
        std::atomic_size_t writers = 0;

        sched::wait_queue_t read_wait;
        sched::wait_queue_t write_wait;
    };

    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags, pid_t pid) override
        {
            lib::unused(pid);
            lib::bug_on(!file);

            const bool rd = is_read(flags);
            const bool wr = is_write(flags);

            if (!(rd ^ wr))
                return std::unexpected { lib::err::invalid_flags };

            if (!file->private_data)
                file->private_data = std::make_shared<data>();

            const auto pdata = std::static_pointer_cast<data>(file->private_data);
            if (rd)
                pdata->readers++;
            if (wr)
                pdata->writers++;

            return { };
        }

        lib::expect<void> close(vfs::file &file) override
        {
            lib::bug_on(!file.private_data);

            const auto pdata = std::static_pointer_cast<data>(file.private_data);
            lib::bug_on(pdata->readers == 0 && pdata->writers == 0);

            if (is_read(file.flags))
            {
                pdata->readers--;
                pdata->write_wait.wake_all();
            }
            else if (is_write(file.flags))
            {
                pdata->writers--;
                pdata->read_wait.wake_all();
            }

            file.private_data.reset();
            return { };
        }

        lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto pdata = std::static_pointer_cast<data>(file->private_data);

            const bool nonblock = (file->flags & o_nonblock) != 0;

            // TODO: packets
            const bool direct = (file->flags & o_direct) != 0;
            if (direct)
                return std::unexpected { lib::err::todo };

            const std::size_t size = std::min(buffer.size_bytes(), data::buffer_size);
            lib::buffer<char> buf { size };

            while (true)
            {
                if (const auto read_bytes = pdata->buffer.pop(buf.span()); read_bytes > 0)
                {
                    buffer.copy_from({
                        reinterpret_cast<std::byte *>(buf.data()),
                        read_bytes
                    });
                    pdata->write_wait.wake_all();
                    return static_cast<std::ssize_t>(read_bytes);
                }

                if (pdata->writers == 0)
                    return 0;

                if (nonblock)
                    return std::unexpected { lib::err::try_again };

                if (pdata->read_wait.wait())
                    return std::unexpected { lib::err::interrupted };
            }
        }

        lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto pdata = std::static_pointer_cast<data>(file->private_data);

            const bool nonblock = (file->flags & o_nonblock) != 0;

            // TODO: packets
            const bool direct = (file->flags & o_direct) != 0;
            if (direct)
                return std::unexpected { lib::err::todo };

            if (pdata->readers == 0)
                // TODO: SIGPIPE
                return std::unexpected { lib::err::no_readers };

            std::size_t total_written = 0;
            const std::size_t count = buffer.size_bytes();
            const std::size_t chunk_size = std::min(count, data::buffer_size);
            lib::buffer<char> buf { chunk_size };

            while (total_written < count)
            {
                const std::size_t current_chunk = std::min(count - total_written, chunk_size);
                buffer.subspan(total_written, current_chunk).copy_to({
                    reinterpret_cast<std::byte *>(buf.data()),
                    current_chunk
                });

                std::size_t chunk_written = 0;
                while (chunk_written < current_chunk)
                {
                    const auto remaining = current_chunk - chunk_written;
                    const auto avail = pdata->buffer.available();
                    const auto to_push = std::min(remaining, avail);

                    if (to_push > 0)
                    {
                        const auto [success, _] = pdata->buffer.push({
                            const_cast<const char *>(buf.data()) + chunk_written,
                            to_push
                        });

                        if (success)
                        {
                            chunk_written += to_push;
                            pdata->read_wait.wake_all();
                            continue;
                        }
                        continue;
                    }

                    if (pdata->readers == 0)
                    {
                        if (total_written + chunk_written > 0)
                            return static_cast<std::ssize_t>(total_written + chunk_written);
                        // TODO: SIGPIPE
                        return std::unexpected { lib::err::no_readers };
                    }

                    if (nonblock)
                    {
                        if (total_written + chunk_written > 0)
                            return static_cast<std::ssize_t>(total_written + chunk_written);
                        return std::unexpected { lib::err::try_again };
                    }

                    if (pdata->write_wait.wait())
                    {
                        if (total_written + chunk_written > 0)
                            return static_cast<std::ssize_t>(total_written + chunk_written);
                        return std::unexpected { lib::err::interrupted };
                    }
                }
                total_written += chunk_written;
            }
            return static_cast<std::ssize_t>(total_written);
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return { };
        }

        // TODO: pipe poll
        // lib::expect<std::uint16_t> poll(std::shared_ptr<file> file, poll_table *pt) override
        // {
        //     lib::unused(file, pt);
        //     return { 0 };
        // }
    };

    std::shared_ptr<vfs::ops> get_ops()
    {
        return ops::singleton();
    }
} // namespace vfs::pipe
