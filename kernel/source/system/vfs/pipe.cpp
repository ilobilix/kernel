// Copyright (C) 2024-2025  ilobilo

module system.vfs.pipe;

import system.vfs;
import lib;
import std;

namespace vfs::pipe
{
    struct data
    {
        static inline constexpr std::size_t buffer_size = 65536;
        // static inline constexpr std::size_t packet_size = 512;

        lib::rbmpmcd<char, buffer_size> buffer;
        // lib::rbmpmcd<std::string, (buffer_size / packet_size)> packets;

        std::atomic_size_t readers = 0;
        std::atomic_size_t writers = 0;

        lib::semaphore read_wait;
        lib::semaphore write_wait;
    };

    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        bool open(std::shared_ptr<vfs::file> self, int flags) override
        {
            lib::bug_on(!self);

            const bool rd = is_read(flags);
            const bool wr = is_write(flags);

            if (!(rd ^ wr))
                return (errno = EINVAL, false);

            if (!self->private_data)
                self->private_data = std::make_shared<data>();

            const auto pdata = std::static_pointer_cast<data>(self->private_data);
            if (rd)
                pdata->readers++;
            if (wr)
                pdata->writers++;

            return true;
        }

        bool close(std::shared_ptr<vfs::file> self) override
        {
            lib::bug_on(!self || !self->private_data);

            const auto pdata = std::static_pointer_cast<data>(self->private_data);
            lib::bug_on(pdata->readers == 0 && pdata->writers == 0);

            if (is_read(self->flags))
                pdata->readers--;
            else if (is_write(self->flags))
                pdata->writers--;

            self->private_data.reset();
            return true;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto pdata = std::static_pointer_cast<data>(file->private_data);

            const bool nonblock = (file->flags & o_nonblock) != 0;

            // TODO: packets
            const bool direct = (file->flags & o_direct) != 0;
            if (direct)
                return (errno = ENOSYS, -1);

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
                    pdata->write_wait.signal_all();
                    return static_cast<std::ssize_t>(read_bytes);
                }

                if (pdata->writers == 0)
                    return 0;

                if (nonblock)
                    return (errno = EAGAIN, -1);

                if (!pdata->read_wait.wait())
                    return (errno = EINTR, -1);
            }
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto pdata = std::static_pointer_cast<data>(file->private_data);

            const bool nonblock = (file->flags & o_nonblock) != 0;

            // TODO: packets
            const bool direct = (file->flags & o_direct) != 0;
            if (direct)
                return (errno = ENOSYS, -1);

            if (pdata->readers == 0)
                // TODO: SIGPIPE
                return (errno = EPIPE, -1);

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
                    const auto pushed = pdata->buffer.push({
                        const_cast<const char *>(buf.data()) + chunk_written,
                        current_chunk - chunk_written
                    });

                    if (pushed > 0)
                    {
                        chunk_written += pushed;
                        pdata->read_wait.signal_all();
                        continue;
                    }

                    if (pdata->readers == 0)
                    {
                        if (total_written + chunk_written > 0)
                            return static_cast<std::ssize_t>(total_written + chunk_written);
                        return (errno = EPIPE, -1);
                    }

                    if (nonblock)
                    {
                        if (total_written + chunk_written > 0)
                            return static_cast<std::ssize_t>(total_written + chunk_written);
                        return (errno = EAGAIN, -1);
                    }

                    if (!pdata->write_wait.wait())
                    {
                        if (total_written + chunk_written > 0)
                            return static_cast<std::ssize_t>(total_written + chunk_written);
                        return (errno = EINTR, -1);
                    }
                }
                total_written += chunk_written;
            }
            return static_cast<std::ssize_t>(total_written);
        }

        bool trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return true;
        }
    };

    std::shared_ptr<vfs::ops> get_ops()
    {
        return ops::singleton();
    }
} // namespace vfs::pipe