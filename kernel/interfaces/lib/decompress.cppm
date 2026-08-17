// Copyright (C) 2024-2026  ilobilo

export module lib:decompress;

import std;

import :error;

export namespace lib
{
    enum class compression_format
    {
        zlib,
        lz4
    };

    class decompressor
    {
        private:
        compression_format _fmt;
        std::shared_ptr<void> _data;

        decompressor(compression_format fmt, std::shared_ptr<void> data)
            : _fmt { fmt }, _data { std::move(data) } { }

        public:
        decompressor(const decompressor &) = delete;
        decompressor(decompressor &&) = default;

        decompressor &operator=(const decompressor &) = delete;
        decompressor &operator=(decompressor &&) = default;

        static expect<decompressor> create(compression_format fmt);

        expect<std::size_t> decompress(std::span<const std::byte> in, std::span<std::byte> out);
        expect<std::size_t> operator()(std::span<const std::byte> in, std::span<std::byte> out)
        {
            return decompress(in, out);
        }
    };
} // export namespace lib
