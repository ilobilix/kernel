// Copyright (C) 2024-2026  ilobilo

module system.vfs.socket;

namespace vfs::socket
{

    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        static std::shared_ptr<socket_t> sock_of(const vfs::file &file)
        {
            return std::static_pointer_cast<socket_t>(file.path.dentry->inode->private_data);
        }

        bool seekable() const override { return false; }

        lib::expect<void> close(vfs::file &file) override
        {
            return sock_of(file)->release();
        }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(offset);
            std::array iovs { buffer };
            msg_header_t hdr {
                .name = { },
                .iovs = iovs,
                .msgctrl = { },
                .out_flags = 0
            };
            return sock_of(*file)->recvmsg(hdr, 0);
        }

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(offset);
            std::array iovs { buffer };
            msg_header_t hdr {
                .name = { },
                .iovs = iovs,
                .msgctrl = { },
                .out_flags = 0
            };
            return sock_of(*file)->sendmsg(hdr, 0);
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return std::unexpected { lib::err::illegal_seek };
        }

        lib::expect<std::uint16_t> poll(
            std::shared_ptr<vfs::file> file, vfs::poll_table *pt
        ) override
        {
            return sock_of(*file)->poll(pt);
        }

        lib::expect<int> ioctl(
            std::shared_ptr<file> file, std::uint64_t request,
            lib::uptr_or_addr argp
        ) override
        {
            return sock_of(*file)->ioctl(request, argp);
        }
    };

    lib::expect<void> register_family(addr_fam af, factory_fn func)
    {
        lib::unused(af, func);
        // TODO
        return std::unexpected { lib::err::todo };
    }

    lib::expect<std::shared_ptr<socket_t>> create(addr_fam af, sock_type type, int protocol)
    {
        lib::unused(af, type, protocol);
        return std::unexpected { lib::err::todo };
    }

    std::shared_ptr<vfs::ops> get_ops()
    {
        return ops::singleton();
    }
} // namespace vfs::socket
