// Copyright (C) 2024-2025  ilobilo

export module lib:partitions;

import :math;
import std;

export namespace lib::part
{
    namespace mbr
    {
        struct [[gnu::packed]] partition
        {
            struct [[gnu::packed]] chs
            {
                std::uint8_t head;
                std::uint8_t sector : 6;
                std::uint16_t cylinder : 10;
            };

            std::uint8_t flags;
            chs chs_start;
            std::uint8_t sid;
            chs chs_last;
            std::uint32_t lba_start;
            std::uint32_t lba_count;

            inline constexpr bool is_bootable()
            {
                return flags == 0x80;
            }

            inline constexpr bool is_pmbr()
            {
                return flags == 0xEE;
            }

            inline constexpr bool is_unused()
            {
                return sid == 0;
            }
        };

        struct table
        {
            std::uint8_t bootstrap[446];
            partition partitions[4];
            std::uint8_t signature[2];

            inline constexpr bool is_read_only()
            {
                return bootstrap[444] == 0x5A && bootstrap[445] == 0x5A;
            }

            inline constexpr bool is_valid()
            {
                return signature[0] == 0x55 && signature[1] == 0xAA;
            }
        };
        static_assert(sizeof(table) == 512);
    } // namespace mbr

    namespace gpt
    {
        struct partition
        {
            uint128_t partguid;
            uint128_t upartguid;
            std::uint64_t lbastart;
            std::uint64_t lbaend;
            std::uint64_t attributes;
            char16_t name[36];

            inline constexpr bool is_unused()
            {
                return partguid == 0;
            }

            inline constexpr bool is_system()
            {
                return attributes & 1;
            }

            inline constexpr bool is_boot()
            {
                return attributes & 2;
            }

            inline constexpr std::u16string_view get_name()
            {
                return name;
            }
        };
        static_assert(sizeof(partition) == 128);

        constexpr char signature[] { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };
        struct [[gnu::packed]] table
        {
            char8_t signature[8];
            std::uint32_t revision;
            std::uint32_t header_size;
            std::uint32_t hdrchecksum;
            std::uint32_t reserved;
            std::uint64_t lba;
            std::uint64_t altlba;
            std::uint64_t firstblock;
            std::uint64_t lastblock;
            uint128_t guid;
            std::uint64_t guidpartlba;
            std::uint32_t parts;
            std::uint32_t partentrysize;
            std::uint32_t partchecksum;

            inline constexpr bool is_valid()
            {
                return std::u8string_view(signature) == signature;
            }
        };
        static_assert(sizeof(table) == 92);
    } // namespace gpt
} // export namespace lib::part