// Copyright (C) 2024-2026  ilobilo

export module ext2:spec;

import lib;
import std;

// definitons are from linux

export namespace ext2
{
    constexpr std::uint16_t magic = 0xEF53;

    constexpr std::size_t superblock_start = 1024;

    constexpr std::size_t ndir_blocks = 12;
    constexpr std::size_t ind_block = ndir_blocks;
    constexpr std::size_t dind_block = ind_block + 1;
    constexpr std::size_t tind_block = dind_block + 1;
    constexpr std::size_t num_blocks = tind_block + 1;

    constexpr std::size_t bad_ino = 1;
    constexpr std::size_t root_ino = 2;
    constexpr std::size_t boot_loader_ino = 5;
    constexpr std::size_t undel_dir_ino = 6;

    constexpr std::size_t name_len = 255;

    enum features : std::uint32_t
    {
        feature_compat_dir_prealloc    = 0x0001,
        feature_compat_imagic_inodes   = 0x0002,
        feature_compat_has_journal     = 0x0004,
        feature_compat_ext_attr        = 0x0008,
        feature_compat_resize_ino      = 0x0010,
        feature_compat_dir_index       = 0x0020,
        feature_compat_any             = 0xFFFFFFFF,

        feature_ro_compat_sparse_super = 0x0001,
        feature_ro_compat_large_file   = 0x0002,
        feature_ro_compat_btree_dir    = 0x0004,
        feature_ro_compat_any          = 0xFFFFFFFF,

        feature_incompat_compression   = 0x0001,
        feature_incompat_filetype      = 0x0002,
        feature_incompat_recover       = 0x0004,
        feature_incompat_journal_dev   = 0x0008,
        feature_incompat_meta_bg       = 0x0010,
        feature_incompat_any           = 0xFFFFFFFF,

        feature_compat_supp            = feature_compat_ext_attr,
        feature_incompat_supp          = (feature_incompat_filetype | feature_incompat_meta_bg),
        feature_ro_compat_supp         = (feature_ro_compat_sparse_super |
                                          feature_ro_compat_large_file |
                                          feature_ro_compat_btree_dir),
        feature_ro_compat_unsupported  = ~feature_ro_compat_supp,
        feature_incompat_unsupported   = ~feature_incompat_supp
    };

    struct superblock_t
    {
        std::uint32_t inodes_count;         // Inodes count
        std::uint32_t blocks_count;         // Blocks count
        std::uint32_t r_blocks_count;       // Reserved blocks count
        std::uint32_t free_blocks_count;    // Free blocks count
        std::uint32_t free_inodes_count;    // Free inodes count
        std::uint32_t first_data_block;     // First Data Block
        std::uint32_t log_block_size;       // Block size
        std::uint32_t log_frag_size;        // Fragment size
        std::uint32_t blocks_per_group;     // # Blocks per group
        std::uint32_t frags_per_group;      // # Fragments per group
        std::uint32_t inodes_per_group;     // # Inodes per group
        std::uint32_t mtime;                // Mount time
        std::uint32_t wtime;                // Write time
        std::uint16_t mnt_count;            // Mount count
        std::uint16_t max_mnt_count;        // Maximal mount count
        std::uint16_t magic;                // Magic signature
        std::uint16_t state;                // File system state
        std::uint16_t errors;               // Behaviour when detecting errors
        std::uint16_t minor_rev_level;      // minor revision level
        std::uint32_t lastcheck;            // time of last check
        std::uint32_t checkinterval;        // max. time between checks
        std::uint32_t creator_os;           // OS
        std::uint32_t rev_level;            // Revision level
        std::uint16_t def_resuid;           // Default uid for reserved blocks
        std::uint16_t def_resgid;           // Default gid for reserved blocks

        // EXT2_DYNAMIC_REV superblocks only
        std::uint32_t first_ino;            // First non-reserved inode
        std::uint16_t inode_size;           // size of inode structure
        std::uint16_t block_group_nr;       // block group # of this superblock
        std::uint32_t feature_compat;       // compatible feature set
        std::uint32_t feature_incompat;     // incompatible feature set
        std::uint32_t feature_ro_compat;    // readonly-compatible feature set
        std::uint8_t uuid[16];              // 128-bit uuid for volume
        char volume_name[16];               // volume name
        char last_mounted[64];              // directory where last mounted
        std::uint32_t algo_usage_bitmap;    // For compression

        // Performance hints
        std::uint8_t prealloc_blocks;       // Nr of blocks to try to preallocate*/
        std::uint8_t prealloc_dir_blocks;   // Nr to preallocate for dirs
        std::uint16_t padding1;

        // Journaling support (ext3)
        std::uint8_t journal_uuid[16];    // uuid of journal superblock
        std::uint32_t journal_inum;       // inode number of journal file
        std::uint32_t journal_dev;        // device number of journal file
        std::uint32_t last_orphan;        // start of list of inodes to delete
        std::uint32_t hash_seed[4];       // HTREE hash seed
        std::uint8_t def_hash_version;    // Default hash version to use
        std::uint8_t reserved_char_pad;
        std::uint16_t reserved_word_pad;
        std::uint32_t default_mount_opts;
        std::uint32_t first_meta_bg;      // First metablock block group
        std::uint32_t reserved[190];      // Padding to the end of the block
    };
    static_assert(sizeof(superblock_t) == 1024);

    struct group_desc_t
    {
        std::uint32_t block_bitmap;      // Blocks bitmap block
        std::uint32_t inode_bitmap;      // Inodes bitmap block
        std::uint32_t inode_table;       // Inodes table block
        std::uint16_t free_blocks_count; // Free blocks count
        std::uint16_t free_inodes_count; // Free inodes count
        std::uint16_t used_dirs_count;   // Directories count
        std::uint16_t pad;
        std::uint32_t reserved[3];
    };
    static_assert(sizeof(group_desc_t) == 32);

    struct inode_t
    {
        std::uint16_t mode;                 // File mode
        std::uint16_t uid;                  // Low 16 bits of Owner Uid
        std::uint32_t size;                 // Size in bytes
        std::uint32_t atime;                // Access time
        std::uint32_t ctime;                // Creation time
        std::uint32_t mtime;                // Modification time
        std::uint32_t dtime;                // Deletion Time
        std::uint16_t gid;                  // Low 16 bits of Group Id
        std::uint16_t links_count;          // Links count
        std::uint32_t blocks;               // Blocks count
        std::uint32_t flags;                // File flags
        union {
            struct {
                std::uint32_t reserved1;
            } linux1;
            struct {
                std::uint32_t translator;
            } hurd1;
            struct {
                std::uint32_t reserved1;
            } masix1;
        } osd1;                             // OS dependent 1
        std::uint32_t block[num_blocks];    // Pointers to blocks
        std::uint32_t generation;           // File version (for NFS)
        std::uint32_t file_acl;             // File ACL
        std::uint32_t dir_acl;              // Directory ACL
        std::uint32_t faddr;                // Fragment address
        union {
            struct {
                std::uint8_t frag;         // Fragment number
                std::uint8_t fsize;        // Fragment size
                std::uint16_t pad1;
                std::uint16_t uid_high;     // these 2 fields
                std::uint16_t gid_high;     // were reserved2[0]
                std::uint32_t reserved2;
            } linux2;
            struct {
                std::uint8_t frag;         // Fragment number
                std::uint8_t fsize;        // Fragment size
                std::uint16_t mode_high;
                std::uint16_t uid_high;
                std::uint16_t gid_high;
                std::uint32_t author;
            } hurd2;
            struct {
                std::uint8_t frag;         // Fragment number
                std::uint8_t fsize;        // Fragment size
                std::uint16_t pad1;
                std::uint32_t reserved2[2];
            } masix2;
        } osd2;                             // OS dependent 2
    };
    static_assert(sizeof(inode_t) == 128);

    enum dir_file_type : std::uint8_t
    {
        ft_unknown = 0,
        ft_reg_file = 1,
        ft_dir = 2,
        ft_chrdev = 3,
        ft_blkdev = 4,
        ft_fifo = 5,
        ft_sock = 6,
        ft_symlink = 7
    };

    // TODO: not supported
    // struct dir_entry_t
    // {
    //     std::uint32_t inode;    // Inode number
    //     std::uint16_t rec_len;  // Directory entry length
    //     std::uint16_t name_len; // Name length */
    //     char name[];            // File name, up to name_len
    // };

    struct dir_entry_2_t
    {
        std::uint32_t inode;    // Inode number
        std::uint16_t rec_len;  // Directory entry length
        std::uint8_t name_len;  // Name length
        std::uint8_t file_type;
        char name[];            // File name, up to name_len
    };
} // export namespace ext2
