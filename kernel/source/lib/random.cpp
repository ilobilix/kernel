// Copyright (C) 2024-2025  ilobilo

module lib;

import :mutex;
import system.time;
import cppstd;

namespace lib
{
    // TODO: better random
    std::ssize_t random_bytes(lib::maybe_uspan<std::byte> buffer)
    {
        static std::mt19937_64 rng { static_cast<std::uint64_t>(time::now().to_ns()) };
        static std::uniform_int_distribution<std::uint8_t> dist { 0, 255 };

        static mutex lock;
        const std::unique_lock _ { lock };

        membuffer buf { std::min(buffer.size_bytes(), 1024uz) };
        std::size_t progress = 0;
        while (progress < buffer.size_bytes())
        {
            const auto chunk_size = std::min(buffer.size_bytes() - progress, buf.size_bytes());
            for (std::size_t i = 0; i < chunk_size; i++)
                buf.data()[i] = static_cast<std::byte>(dist(rng));
            if (!buffer.subspan(progress, chunk_size).copy_from(buf.data()))
                return -1;
            progress += chunk_size;
        }
        return static_cast<std::ssize_t>(progress);
    }
} // namespace lib