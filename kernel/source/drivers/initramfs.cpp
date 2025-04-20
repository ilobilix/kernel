// Copyright (C) 2024-2025  ilobilo

module drivers.initramfs;

import system.vfs;
import boot;
import lib;
import cppstd;

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
            auto current = static_cast<header *>(ptr);
            while (magic == std::string_view { current->magic, 6 })
            {
                std::string_view name { current->name };
                std::string_view linkname { current->linkname };

                auto mode = lib::oct2int<mode_t>(current->mode);
                auto size = lib::oct2int<std::size_t>(current->size);
                auto mtim = lib::oct2int<time_t>(current->mtime);

                // auto devmajor = lib::oct2int<time_t>(current->devmajor);
                // auto devminor = lib::oct2int<time_t>(current->devminor);

                std::shared_ptr<vfs::node> node;

                if (name == "./")
                    goto next;

                switch (current->typeflag)
                {
                    case types::regular:
                    case types::aregular:
                    {
                        auto ret = vfs::create(nullptr, name, mode | stat::type::s_ifreg);
                        if (!ret)
                            log::error("ustar: could not create a regular file '{}'", name);

                        node = ret.value();
                        const std::span data { reinterpret_cast<std::byte *>(reinterpret_cast<std::uintptr_t>(current) + 512), size };
                        if (node->backing->op->write(node->backing, 0, data) != std::ssize_t(size))
                        {
                            log::error("ustar: could not write to a regular file '{}'", name);
                            // TODO: remove node
                        }
                        break;
                    }
                    case types::hardlink:
                    {
                        auto ret = vfs::link(nullptr, name, nullptr, linkname);
                        if (!ret)
                            log::error("ustar: could not create a hardlink '{}' -> '{}'", name, linkname);
                        node = ret.value();
                        break;
                    }
                    case types::symlink:
                    {
                        auto ret = vfs::symlink(nullptr, name, linkname);
                        if (!ret)
                            log::error("ustar: could not create a symlink '{}' -> '{}'", name, linkname);
                        node = ret.value();
                        break;
                    }
                    case types::chardev:
                        lib::panic("ustar: TODO: chardev");
                        break;
                    case types::blockdev:
                        lib::panic("ustar: TODO: blockdev");
                        break;
                    case types::directory:
                    {
                        auto ret = vfs::create(nullptr, name, mode | stat::type::s_ifdir);
                        if (!ret)
                            log::error("ustar: could not create a directory '{}'", name);
                        node = ret.value();
                        break;
                    }
                    case types::fifo:
                        lib::panic("ustar: TODO: fifo");
                        break;
                }

                if (node != nullptr)
                    node->backing->stat.st_mtim = timespec { mtim, 0 };

                next:
                current = reinterpret_cast<header *>(reinterpret_cast<std::uintptr_t>(current) + 512 + lib::align_up(size, 512zu));
            }
            return true;
        }
    } // namespace ustar

    void init()
    {
        auto module = boot::find_module("initramfs");
        if (module == nullptr)
            lib::panic("could not find initramfs");

        if (!ustar::load(lib::tohh(module->address)))
            lib::panic("could not load initramfs as ustar archive");
    }
} // namespace initramfs