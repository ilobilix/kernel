// Copyright (C) 2024-2026  ilobilo

export module squashfs:spec;

import lib;
import std;

export namespace squashfs
{
    constexpr std::uint32_t magic = 0x73717368;

    constexpr std::uint16_t version_major = 4;
    constexpr std::uint16_t version_minor = 0;

    constexpr std::uint64_t no_table = -1;
    constexpr std::uint32_t no_xattr = -1;
    constexpr std::uint32_t no_frag = -1;

    constexpr std::size_t max_symlen = 65535;
    constexpr std::size_t max_namelen = 256;
    constexpr std::size_t max_dir_count = 256;

    constexpr std::size_t min_datablk = lib::kib(4);
    constexpr std::size_t max_datablk = lib::mib(1);

    constexpr std::size_t metadata_size = lib::kib(8);
    constexpr std::uint16_t metadata_uncompressed = 1u << 15;
    constexpr std::uint16_t metadata_size_mask = metadata_uncompressed - 1;

    enum class compressor : std::uint16_t
    {
        zlib = 1, // no gzip headers
        lzma = 2, // version 1
        lzo = 3,
        xz = 4,   // version 2
        lz4 = 5,
        zstd = 6
    };

    enum class flag : std::uint16_t
    {
        none = 0x0000,
        uncompressed_inodes = 0x0001,
        uncompressed_data = 0x0002,
        check_data = 0x0004, // unused and unset
        uncompressed_frags = 0x0008,
        no_frags = 0x0010,
        always_frags = 0x0020,
        deduplicated_data = 0x0040,
        has_export_table = 0x0080,
        uncompressed_xattrs = 0x0100,
        no_xattrs = 0x0200,
        has_compress_options = 0x0400, // only flag needed
        uncompressed_id_table = 0x0800
    };

    struct superblock_t
    {
        std::uint32_t magic;
        std::uint32_t inodes;
        std::uint32_t mtime; // in seconds
        std::uint32_t block_size; // power of 2 between 4kib and 1mib
        std::uint32_t frags;
        compressor compressor;
        std::uint16_t block_log; // needs to agree with block_size
        flag flags;
        std::uint16_t ids;
        std::uint16_t vermaj;
        std::uint16_t vermin;
        std::uint64_t root_ino;
        std::uint64_t bytes_used;
        // byte offsets
        std::uint64_t id_table;
        std::uint64_t xattr_table; // optional
        std::uint64_t inode_table;
        std::uint64_t dir_table;
        std::uint64_t frag_table; // optional
        std::uint64_t export_table; // optional
    };
    static_assert(sizeof(superblock_t) == 96);

    enum class inode_type : std::uint16_t
    {
        basic_dir = 1,
        basic_file = 2,
        basic_symlink = 3,
        basic_blkdev = 4,
        basic_chardev = 5,
        basic_fifo = 6,
        basic_sock = 7,
        extended_dir = 8,
        extended_file = 9,
        extended_symlink = 10,
        extended_blkdev = 11,
        extended_chardev = 12,
        extended_fifo = 13,
        extended_sock = 14
    };

    constexpr inode_type to_basic(inode_type type)
    {
        lib::bug_on(type < inode_type::basic_dir || type > inode_type::extended_sock);
        if (type <= inode_type::basic_sock)
            return type;

        return static_cast<inode_type>(
            std::to_underlying(type) - std::to_underlying(inode_type::basic_sock)
        );
    }

    struct inode_t
    {
        inode_type type;
        std::uint16_t perms;
        std::uint16_t uid;
        std::uint16_t gid;
        std::uint32_t mtime;
        std::uint32_t ino;
    };
    static_assert(sizeof(inode_t) == 16);

    struct dir_inode_t : inode_t
    {
        std::uint32_t block_idx;
        std::uint32_t links;
        std::uint16_t size; // uncompressed in directory table. if <4 then it's empty
        std::uint16_t block_off; // uncompressed offset into the block (block_idx)
        std::uint32_t parent_ino;
    };
    static_assert(sizeof(dir_inode_t) == 32);

    struct ext_dir_inode_t : inode_t
    {
        std::uint32_t links;
        std::uint32_t size;
        std::uint32_t block_idx;
        std::uint32_t parent_ino;
        std::uint16_t index_count; // number of dir index entries following the inode
        std::uint16_t block_off;
        std::uint32_t xattr_idx;
    };
    static_assert(sizeof(ext_dir_inode_t) == 40);

    struct file_inode_t : inode_t
    {
        std::uint32_t block_start; // offset to the data block
        std::uint32_t frag_idx; // index into the fragment table where tail end of this file is
        std::uint32_t block_off; // uncompressed offset in the block where tail end of this file is
        std::uint32_t size;
        std::uint32_t block_sizes[];

        // blocks = (frag_idx == no_frag) ? ceil(size / block_size) : floor(size / block_size)
        // if frag_idx then tail end size = size % block_size

        // accessing data:
        //   index = floor(offset / block_size)
        //   location = block_start
        //   for (i = 0; i < index; i++)
        //       location += block_sizes[i] & 0x00FFFFFF
    };
    static_assert(sizeof(file_inode_t) == 32);

    struct ext_file_inode_t : inode_t
    {
        std::uint64_t blocks_start;
        std::uint64_t size;
        std::uint64_t sparse;
        std::uint32_t links;
        std::uint32_t frag_idx;
        std::uint32_t block_off;
        std::uint32_t xattr_idx;
        std::uint32_t block_sizes[];
    };
    static_assert(sizeof(ext_file_inode_t) == 56);

    struct slink_inode_t : inode_t
    {
        std::uint32_t links;
        std::uint32_t size;
        std::uint8_t path[]; // not null terminated

        std::size_t xattr_idx_off() const { return sizeof(*this) + size; }
    };
    static_assert(sizeof(slink_inode_t) == 24);

    struct dev_inode_t : inode_t
    {
        std::uint32_t links;
        std::uint32_t dev;
    };
    static_assert(sizeof(dev_inode_t) == 24);

    struct ext_dev_inode_t : inode_t
    {
        std::uint32_t links;
        std::uint32_t dev;
        std::uint32_t xattr_idx;
    };
    static_assert(sizeof(ext_dev_inode_t) == 28);

    struct ipc_inode_t : inode_t
    {
        std::uint32_t links;
    };
    static_assert(sizeof(ipc_inode_t) == 20);

    struct ext_ipc_inode_t : inode_t
    {
        std::uint32_t links;
        std::uint32_t xattr_idx;
    };
    static_assert(sizeof(ext_ipc_inode_t) == 24);

    struct dir_hdr_t
    {
        std::uint32_t count; // stored off by -1
        std::uint32_t start; // metadata block in inode table. relative
        std::uint32_t ino; // entries store their ino as a difference to this
    };
    static_assert(sizeof(dir_hdr_t) == 12);

    struct dir_entry_t
    {
        std::uint16_t offset; // into the uncompressed metadata block
        std::int16_t ino_off; // difference of this ino to reference stored in hdr
        inode_type type; // basic is stored even for extended
        std::uint16_t name_size; // one less than the size of the entry name
        std::uint8_t name[]; // not null terminated. name_size + 1 bytes
    };
    static_assert(sizeof(dir_entry_t) == 8);

    struct dir_index_t
    {
        std::uint32_t index; // byte offset from first directory header
        std::uint32_t start;
        std::uint32_t name_size; // same as in dir_entry_t
        std::uint8_t name[];
    };
    static_assert(sizeof(dir_index_t) == 12);

    struct fragment_t
    {
        std::uint64_t start; // fragment block offset in archive
        std::uint32_t size; // if block is uncompressed then bit 24 is set
        std::uint32_t unused; // should be 0
    };
    static_assert(sizeof(fragment_t) == 16);

    // TODO
    // enum class ext_attr_type : std::uint16_t
    // {
    //     user = 0, // prefix with "user."
    //     trusted = 1,
    //     security = 2
    // };

    // struct ext_attr_key_t
    // {
    //     ext_attr_type type;
    //     std::uint16_t name_size; // number of bytes
    //     std::uint8_t name[];
    // };

    // struct ext_attr_val_t
    // {
    //     std::uint32_t size;
    //     std::uint8_t value[];
    // };

    // compression options. there is not one for lzma so the section must not be present
    struct zlib_t
    {
        enum class strat : std::uint16_t
        {
            default_ = 0x0001,
            filtered = 0x0002,
            huffman_only = 0x0004,
            run_length_encoded = 0x0008,
            fixed = 0x0010
        };

        std::uint32_t level; // 1-9
        std::uint16_t window; // 8-15
        strat strategies;
    };

    struct xz_t
    {
        enum class filter : std::uint32_t
        {
            x86 = 0x0001,
            powerpc = 0x0002,
            ia64 = 0x0004,
            arm = 0x0008,
            arm_thumb = 0x0010,
            sparc = 0x0020,
            arm64 = 0x0040,
            riscv = 0x0080
        };

        std::uint32_t dirsize; // >=8kib, pow2 or sum of two consecutive pow2s
        filter filters;
    };

    // not optional
    struct lz4_t
    {
        std::uint32_t version; // must be 1
        std::uint32_t flags; // 1 == high compression mode
    };

    struct zstd_t
    {
        std::int32_t level; // 1-22
    };

    struct lzo_t
    {
        enum class algo : std::uint32_t
        {
            lzo1x_1_1 = 0,
            lzo1x_1_11 = 1,
            lzo1x_1_12 = 2,
            lzo1x_1_15 = 3,
            lzo1x_999 = 4
        };
        algo algorithm;
        std::uint32_t level; // 0-9 for lzo1x_999
    };
} // export namespace squashfs
