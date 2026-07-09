// Copyright (C) 2024-2026  ilobilo

export module system.dev.block;

import system.memory.virt;
import system.dev;
import system.vfs;
import libarch;
import lib;
import std;

export namespace dev::block
{
    class drive_t
    {
        friend lib::expect<void> register_drive(
            std::shared_ptr<drive_t> drive, std::string_view part_prefix
        );

        protected:
        std::uint8_t _lba_shift;
        std::uint64_t _lba_count;
        std::uint64_t _max_transfer_lba;

        arch::dma_pool &_pool;

        std::vector<std::shared_ptr<dev::device_t>> _parts;

        virtual dev_t alloc_id() = 0;

        virtual void rw(
            bool write, bool sync, std::uint64_t lba, arch::dma_buffer &buffer,
            std::function<void (lib::expect<void>)> cb
        ) = 0;

        lib::expect<void> rw(
            bool write, bool sync, std::uint64_t offset, std::size_t total_size,
            std::function_ref<lib::maybe_uspan<std::byte> (std::size_t)> getter
        );

        public:
        std::shared_ptr<dev::device_t> dev;

        drive_t(
            std::uint8_t lba_shift, std::uint64_t lba_count,
            std::uint64_t max_transfer_lba, arch::dma_pool &pool
        ) : _lba_shift { lba_shift }, _lba_count { lba_count },
            _max_transfer_lba { max_transfer_lba }, _pool { pool } { }

        virtual ~drive_t() = default;

        std::uint32_t block_size() const { return 1u << _lba_shift; }
        std::uint64_t block_count() const { return _lba_count; }
        std::uint64_t size_bytes() const { return _lba_count << _lba_shift; }

        std::span<const std::shared_ptr<dev::device_t>> partitions() const { return _parts; }

        template<std::ranges::random_access_range Range>
            requires std::same_as<std::ranges::range_value_t<Range>, lib::maybe_uspan<std::byte>>
        lib::expect<void> rw(bool write, bool sync, std::uint64_t offset, Range &&range)
        {
            std::size_t total_size = 0;
            for (const auto &uspan : range)
                total_size += uspan.size();
            return rw(write, sync, offset, total_size, [&](std::size_t idx) { return range[idx]; });
        }

        virtual lib::expect<void> flush() { return { }; };
    };

    struct object_t : vmm::object
    {
        friend struct ops_t;

        private:
        std::weak_ptr<drive_t> drive;
        std::uint64_t lba_start;
        std::uint64_t lba_count;

        lib::expect<void> fetch_pages(std::size_t idx, std::span<vmm::page *> pages) override;
        lib::expect<void> write_pages(std::size_t idx, std::span<vmm::page *> pages) override;

        public:
        object_t(std::uint64_t lba_start, std::uint64_t lba_count, std::weak_ptr<drive_t> drive)
            : vmm::object { vmm::object_type::file },
              drive { drive }, lba_start { lba_start }, lba_count { lba_count } { }
    };

    struct ops_t : vfs::ops
    {
        private:
        object_t::ptr memory;

        object_t &get_memory() { return static_cast<object_t &>(*memory); }

        public:
        ops_t(
            std::shared_ptr<drive_t> drive,
            std::optional<std::uint64_t> lba_start = std::nullopt,
            std::optional<std::uint64_t> lba_count = std::nullopt
        ) : vfs::ops { }, memory { new object_t {
                lba_start.value_or(0),
                lba_count.value_or(drive->block_count()),
                std::move(drive)
            } } { }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file> file) override;
    };

    namespace mbr
    {
        struct [[gnu::packed]] partition_t
        {
            struct [[gnu::packed]] chs_t
            {
                std::uint8_t head;
                std::uint8_t sector : 6;
                std::uint16_t cylinder : 10;
            };

            std::uint8_t flags;
            chs_t chs_start;
            std::uint8_t sid;
            chs_t chs_last;
            std::uint32_t lbastart;
            std::uint32_t lbacount;

            inline constexpr bool is_bootable() const
            {
                return flags == 0x80;
            }

            inline constexpr bool is_pmbr() const
            {
                return sid == 0xEE;
            }

            inline constexpr bool is_extended() const
            {
                return sid == 0x05 || sid == 0x0F;
            }

            inline constexpr bool is_unused() const
            {
                return sid == 0;
            }
        };

        struct table_t
        {
            std::uint8_t bootstrap[446];
            partition_t partitions[4];
            std::uint8_t signature[2];

            inline constexpr bool is_read_only() const
            {
                return bootstrap[444] == 0x5A && bootstrap[445] == 0x5A;
            }

            inline constexpr bool is_valid() const
            {
                return signature[0] == 0x55 && signature[1] == 0xAA;
            }
        };
        static_assert(sizeof(table_t) == 512);
    } // namespace mbr

    namespace gpt
    {
        struct partition_t
        {
            uint128_t partguid;
            uint128_t upartguid;
            std::uint64_t lbastart;
            std::uint64_t lbaend;
            std::uint64_t attributes;
            char16_t name[36];

            inline constexpr bool is_unused() const
            {
                return partguid == 0;
            }

            inline constexpr bool is_system() const
            {
                return attributes & 1;
            }

            inline constexpr bool is_boot() const
            {
                return attributes & 4;
            }

            inline constexpr std::u16string_view get_name() const
            {
                std::u16string_view str { name, std::size(name) };
                if (auto pos = str.find(u'\0'); pos != std::u16string_view::npos)
                    return str.substr(0, pos);
                return str;
            }
        };
        static_assert(sizeof(partition_t) == 128);

        constexpr char signature[] { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };
        struct [[gnu::packed]] table_t
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
                if (std::u8string_view(signature) != signature)
                    return false;

                const auto old = hdrchecksum;
                hdrchecksum = 0;
                const auto csum = lib::crc32::compute(std::span {
                    reinterpret_cast<std::byte *>(this), header_size
                });
                return csum == (hdrchecksum = old);
            }
        };
        static_assert(sizeof(table_t) == 92);
    } // namespace gpt

    lib::expect<void> register_drive(std::shared_ptr<drive_t> drive, std::string_view part_prefix);
    bool unregister_drive(std::shared_ptr<drive_t> drive);

    class_t &get_class();
    std::uint32_t alloc_minor();
} // export namespace dev::block
