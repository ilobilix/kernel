// Copyright (C) 2024-2025  ilobilo

module drivers.initramfs;

import drivers.fs.dev;
import system.dev;
import system.vfs;
import magic_enum;
import boot;
import lib;
import std;

namespace initramfs
{
    namespace ustar
    {
        constexpr std::string_view magic { "ustar", 6 };
        // constexpr std::string_view version { "00", 2 };

        enum types : char
        {
            regular = '0',
            aregular = '\0',
            hardlink = '1',
            symlink = '2',
            chardev = '3',
            blockdev = '4',
            directory = '5',
            fifo = '6',
            control = '7',
            xhd = 'x',
            xgl = 'g'
        };

        struct [[gnu::packed]] header
        {
            char name[100];
            char mode[8];
            char uid[8];
            char gid[8];
            char size[12];
            char mtime[12];
            char chksum[8];
            char typeflag;
            char linkname[100];
            char magic[6];
            char version[2];
            char uname[32];
            char gname[32];
            char devmajor[8];
            char devminor[8];
            char prefix[155];
        };

        bool load(void *ptr)
        {
            log::info("ustar: extracting initramfs");

            auto current = static_cast<header *>(ptr);
            while (magic == std::string_view { current->magic, 6 })
            {
                const std::string_view name { current->name };
                const std::string_view linkname { current->linkname };

                const auto mode = lib::oct2int<mode_t>(current->mode);
                const auto size = lib::oct2int<std::size_t>(current->size);
                const auto mtim = lib::oct2int<time_t>(current->mtime);

                const auto devmajor = lib::oct2int<time_t>(current->devmajor);
                const auto devminor = lib::oct2int<time_t>(current->devminor);
                const dev_t dev = dev::makedev(devmajor, devminor);

                std::shared_ptr<vfs::inode> inode;

                if (name == "./")
                    goto next;

                switch (current->typeflag)
                {
                    case types::regular:
                    case types::aregular:
                    {
                        auto ret = vfs::create(std::nullopt, name, mode | stat::type::s_ifreg);
                        if (!ret)
                        {
                            log::error(
                                "ustar: could not create a regular file '{}': {}",
                                name, magic_enum::enum_name(ret.error())
                            );
                            break;
                        }

                        const auto data = lib::maybe_uspan<std::byte>::create(
                            reinterpret_cast<std::byte *>(
                                reinterpret_cast<std::uintptr_t>(current) + 512
                            ), size
                        ).value();

                        auto file = vfs::file::create(ret.value(), 0, 0, 0);
                        if (file->pwrite(0, data) != std::ssize_t(size))
                        {
                            log::error("ustar: could not write to a regular file '{}'", name);
                            if (const auto ret = vfs::unlink(std::nullopt, name); !ret)
                            {
                                log::error(
                                    "ustar: could not unlink incomplete regular file '{}': {}",
                                    name, magic_enum::enum_name(ret.error())
                                );
                            }
                            break;
                        }
                        inode = ret.value().dentry->inode;
                        break;
                    }
                    case types::hardlink:
                    {
                        auto ret = vfs::link(std::nullopt, name, std::nullopt, linkname);
                        if (!ret)
                        {
                            log::error(
                                "ustar: could not create a hardlink '{}' -> '{}': {}",
                                name, linkname, magic_enum::enum_name(ret.error())
                            );
                            break;
                        }
                        inode = ret.value().dentry->inode;
                        break;
                    }
                    case types::symlink:
                    {
                        auto ret = vfs::symlink(std::nullopt, name, linkname);
                        if (!ret)
                        {
                            log::error(
                                "ustar: could not create a symlink '{}' -> '{}': {}",
                                name, linkname, magic_enum::enum_name(ret.error())
                            );
                            break;
                        }
                        inode = ret.value().dentry->inode;
                        break;
                    }
                    case types::chardev:
                    {
                        auto ret = vfs::create(std::nullopt, name, mode | stat::type::s_ifchr, dev);
                        if (!ret)
                        {
                            log::error(
                                "ustar: could not create a character device file '{}': {}",
                                name, magic_enum::enum_name(ret.error())
                            );
                            break;
                        }
                        inode = ret.value().dentry->inode;
                        break;
                    }
                    case types::blockdev:
                    {
                        auto ret = vfs::create(std::nullopt, name, mode | stat::type::s_ifblk, dev);
                        if (!ret)
                        {
                            log::error(
                                "ustar: could not create a block device file '{}': {}",
                                name, magic_enum::enum_name(ret.error())
                            );
                            break;
                        }
                        inode = ret.value().dentry->inode;
                        break;
                    }
                    case types::directory:
                    {
                        auto ret = vfs::create(std::nullopt, name, mode | stat::type::s_ifdir);
                        if (!ret)
                        {
                            log::error(
                                "ustar: could not create a directory '{}': {}",
                                name, magic_enum::enum_name(ret.error())
                            );
                            break;
                        }
                        inode = ret.value().dentry->inode;
                        break;
                    }
                    case types::fifo:
                        lib::panic("ustar: TODO: fifo");
                        break;
                }

                if (inode != nullptr)
                    inode->stat.st_mtim = timespec { mtim, 0 };

                next:
                current = reinterpret_cast<header *>(reinterpret_cast<std::uintptr_t>(current) + 512 + lib::align_up(size, 512zu));
            }
            return true;
        }
    } // namespace ustar

    lib::initgraph::stage *extracted_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.initramfs.extracted",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task init_task
    {
        "vfs.initramfs.extract",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            vfs::root_mounted_stage(),
            fs::dev::registered_stage()
        },
        lib::initgraph::entail { extracted_stage() },
        [] {
            auto module = boot::find_module("initramfs");
            if (module == nullptr)
                lib::panic("could not find initramfs");

            if (!ustar::load(lib::tohh(module->address)))
                lib::panic("could not load initramfs as ustar archive");
        }
    };
} // namespace initramfs