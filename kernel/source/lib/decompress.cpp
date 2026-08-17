// Copyright (C) 2024-2026  ilobilo

module;

#include <lz4.h>
#include <miniz.h>

module lib;

namespace lib
{
    expect<decompressor> decompressor::create(compression_format fmt)
    {
        switch (fmt)
        {
            case compression_format::zlib:
                return decompressor { fmt, std::make_shared<tinfl_decompressor>() };
            case compression_format::lz4:
                return decompressor { fmt, nullptr };
        }
        return std::unexpected { lib::err::invalid_argument };
    }

    expect<std::size_t> decompressor::decompress(
        std::span<const std::byte> in, std::span<std::byte> out
    )
    {
        switch (_fmt)
        {
            case compression_format::zlib:
            {
                if (!_data)
                    return std::unexpected { lib::err::invalid_argument };

                auto decomp = static_cast<tinfl_decompressor *>(_data.get());
                tinfl_init(decomp);

                auto insize = in.size();
                auto outsize = out.size();

                const auto result = tinfl_decompress(
                    decomp,
                    reinterpret_cast<const mz_uint8 *>(in.data()),
                    &insize,
                    reinterpret_cast<mz_uint8 *>(out.data()),
                    reinterpret_cast<mz_uint8 *>(out.data()),
                    &outsize,
                    TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF
                );

                if (result == TINFL_STATUS_HAS_MORE_OUTPUT)
                    return std::unexpected { lib::err::buffer_too_small };

                if (result != TINFL_STATUS_DONE || in.size() != insize)
                    return std::unexpected { lib::err::corrupted_data };
                return outsize;
            }
            case compression_format::lz4:
            {
                constexpr auto max = std::numeric_limits<int>::max();
                if (in.size() > max || out.size() > max)
                    return std::unexpected { lib::err::invalid_length };

                const auto result = LZ4_decompress_safe(
                    reinterpret_cast<const char *>(in.data()),
                    reinterpret_cast<char *>(out.data()),
                    in.size(), out.size()
                );

                if (result < 0)
                    return std::unexpected { lib::err::corrupted_data };
                return result;
            }
        }
        lib::panic("invalid decompressor format");
        std::unreachable();
    }
} // namespace lib
