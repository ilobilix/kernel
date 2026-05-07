// Copyright (C) 2024-2026  ilobilo

module drivers.initramfs;

import system.vfs;
import system.vfs.dev;
import boot;
import lib;
import std;

namespace initramfs
{
    namespace ustar
    {
        constexpr std::size_t block_size = 512;
        constexpr std::string_view magic { "ustar", 5 };
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
            longlink = 'K',
            longname = 'L',
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

            bool is_gnu() const { return magic[5] == ' '; }

            std::byte *payload() const
            {
                return reinterpret_cast<std::byte *>(
                    const_cast<header *>(this)
                ) + block_size;
            }
        };

        template<typename Type> requires std::is_array_v<Type>
        constexpr std::string_view get_string(const Type &member)
        {
            return std::string_view {
                member, std::strnlen(member, sizeof(member))
            };
        }

        header *advance(header *cur, std::size_t payload_size)
        {
            return reinterpret_cast<header *>(
                cur->payload() + lib::align_up(payload_size, block_size)
            );
        }

        bool load(std::span<std::byte> data)
        {
            lib::info("ustar: extracting initramfs");

            const auto data_end = data.data() + data.size();
            const auto fits = [&](const header *cur) {
                return reinterpret_cast<const std::byte *>(cur) + sizeof(header) <= data_end;
            };

            std::string long_name;
            std::string long_linkname;
            bool have_long_name = false;
            bool have_long_linkname = false;

            auto current = reinterpret_cast<header *>(data.data());
            while (fits(current) && magic == std::string_view { current->magic, 5 })
            {
                if (current->name[0] == '\0')
                    break;

                const auto payload_size = lib::oct2int<std::size_t>(current->size);
                if (current->payload() + payload_size > data_end)
                {
                    lib::error("ustar: payload extends past end of archive");
                    return false;
                }

                if (current->typeflag == types::longname || current->typeflag == types::longlink)
                {
                    const auto ptr = reinterpret_cast<const char *>(current->payload());
                    const bool is_name = (current->typeflag == types::longname);
                    auto &dst = is_name ? long_name : long_linkname;
                    auto &flag = is_name ? have_long_name : have_long_linkname;
                    dst.assign(ptr, std::strnlen(ptr, payload_size));
                    flag = true;

                    current = advance(current, payload_size);
                    continue;
                }

                std::string name_buf;
                std::string_view name;

                if (have_long_name)
                    name = long_name;
                else if (!current->is_gnu() && current->prefix[0] != '\0')
                {
                    const auto prefix = get_string(current->prefix);
                    const auto nam = get_string(current->name);

                    name_buf.reserve(prefix.length() + 1 + nam.length());
                    name_buf.append(prefix).append("/").append(nam);
                    name = name_buf;
                }
                else name = get_string(current->name);

                const auto linkname {
                    have_long_linkname
                        ? std::string_view { long_linkname }
                        : get_string(current->linkname)
                };

                const auto mode = lib::oct2int<mode_t>(current->mode);
                const auto uid = lib::oct2int<uid_t>(current->uid);
                const auto gid = lib::oct2int<gid_t>(current->gid);
                const auto mtim = lib::oct2int<time_t>(current->mtime);

                const auto devmajor = lib::oct2int<time_t>(current->devmajor);
                const auto devminor = lib::oct2int<time_t>(current->devminor);
                const dev_t dev = vfs::dev::makedev(devmajor, devminor);

                std::shared_ptr<vfs::inode> inode;

                if (name == "./" || name.ends_with("/.keep"))
                    goto next;

                switch (current->typeflag)
                {
                    case types::regular:
                    case types::aregular:
                    {
                        auto ret = vfs::create(std::nullopt, name, mode | stat::type::s_ifreg);
                        if (!ret)
                        {
                            lib::error(
                                "ustar: could not create regular file '{}': {}",
                                name, lib::error_name(ret.error())
                            );
                            break;
                        }

                        const auto payload = lib::maybe_uspan<std::byte>::create(
                            current->payload(), payload_size
                        ).value();

                        auto file = vfs::file::create(ret.value(), 0, 0);
                        if (const auto res = file->pwrite(0, payload); !res.has_value() || res.value() != payload_size)
                        {
                            lib::error(
                                "ustar: could not write to regular file '{}': {}",
                                name, res.has_value() ? "size mismatch" : lib::error_name(res.error())
                            );

                            if (const auto ret = vfs::unlink(std::nullopt, name); !ret)
                            {
                                lib::error(
                                    "ustar: could not unlink incomplete regular file '{}': {}",
                                    name, lib::error_name(ret.error())
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
                            lib::error(
                                "ustar: could not create hardlink '{}' -> '{}': {}",
                                name, linkname, lib::error_name(ret.error())
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
                            lib::error(
                                "ustar: could not create symlink '{}' -> '{}': {}",
                                name, linkname, lib::error_name(ret.error())
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
                            lib::error(
                                "ustar: could not create character device file '{}': {}",
                                name, lib::error_name(ret.error())
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
                            lib::error(
                                "ustar: could not create block device file '{}': {}",
                                name, lib::error_name(ret.error())
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
                            lib::error(
                                "ustar: could not create directory '{}': {}",
                                name, lib::error_name(ret.error())
                            );
                            break;
                        }
                        inode = ret.value().dentry->inode;
                        break;
                    }
                    case types::fifo:
                        lib::panic("ustar: TODO: fifo");
                        break;
                    case types::xhd:
                    case types::xgl:
                        lib::panic("ustar: TODO: extended header");
                        break;
                    default:
                        lib::error("ustar: unknown typeflag '{}' for file '{}'", current->typeflag, name);
                        break;
                }

                if (inode != nullptr)
                {
                    inode->stat.st_uid = uid;
                    inode->stat.st_gid = gid;
                    inode->stat.st_mtim = timespec { mtim, 0 };
                }

                next:
                long_name.clear();
                long_linkname.clear();
                have_long_name = false;
                have_long_linkname = false;
                current = advance(current, payload_size);
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
        lib::initgraph::require { vfs::root_mounted_stage() },
        lib::initgraph::entail { extracted_stage() },
        [] {
            auto module = boot::find_module("initramfs");
            if (module == nullptr)
                lib::panic("could not find initramfs");

            std::span<std::byte> data {
                reinterpret_cast<std::byte *>(module->address),
                module->size
            };

            if (!ustar::load(data))
                lib::panic("could not load initramfs as ustar archive");
        }
    };
} // namespace initramfs
