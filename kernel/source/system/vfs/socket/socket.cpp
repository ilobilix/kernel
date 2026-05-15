// Copyright (C) 2024-2026  ilobilo

module system.vfs.socket;

import drivers.fs.procfs;
import system.sched;

namespace vfs::socket
{
    namespace
    {
        lib::locker<
            std::array<
                family_t *,
                af_max
            >, lib::spinlock
        > registry { };

        struct ops : vfs::ops
        {
            static std::shared_ptr<ops> singleton()
            {
                static auto instance = std::make_shared<ops>();
                return instance;
            }

            bool seekable() const override { return false; }

            lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags, pid_t pid) override
            {
                lib::unused(file, flags, pid);
                return std::unexpected { lib::err::invalid_device_or_address };
            }

            lib::expect<void> close(vfs::file &file) override
            {
                return from_file(file)->release();
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
                    .msgctrl_len_out = 0,
                    .addr_len_out = 0,
                    .out_flags = 0
                };

                const auto flags = (file->flags & o_nonblock) ? msg_dontwait : 0;
                return from_file(*file)->recvmsg(hdr, flags);
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
                    .msgctrl_len_out = 0,
                    .addr_len_out = 0,
                    .out_flags = 0
                };

                const auto flags = (file->flags & o_nonblock) ? msg_dontwait : 0;
                return from_file(*file)->sendmsg(hdr, flags);
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
                return from_file(*file)->poll(pt);
            }

            lib::expect<int> ioctl(
                std::shared_ptr<file> file, std::uint64_t request,
                lib::uptr_or_addr argp
            ) override
            {
                return from_file(*file)->ioctl(request, argp);
            }
        };
    } // namespace

    std::shared_ptr<socket_t> from_file(const vfs::file &file)
    {
        return std::static_pointer_cast<socket_t>(file.private_data);
    }

    lib::expect<void> register_family(family_t *fam)
    {
        auto locked = registry.lock();
        if ((*locked)[fam->af])
            return std::unexpected { lib::err::already_exists };
        (*locked)[fam->af] = fam;
        return { };
    }

    auto create(addr_fam af, sock_type type, int protocol)
        -> lib::expect<std::shared_ptr<socket_t>>
    {
        if (af >= af_max)
            return std::unexpected { lib::err::address_family_unsupported };

        family_t *fam = nullptr;
        {
            const auto locked = registry.lock();
            fam = (*locked)[af];
        }
        if (!fam)
            return std::unexpected { lib::err::address_family_unsupported };

        return fam->create(type, protocol);
    }

    auto create_pair(addr_fam af, sock_type type, int protocol)
        -> lib::expect<std::pair<std::shared_ptr<socket_t>, std::shared_ptr<socket_t>>>
    {
        if (af >= af_max)
            return std::unexpected { lib::err::address_family_unsupported };

        family_t *fam = nullptr;
        {
            const auto locked = registry.lock();
            fam = (*locked)[af];
        }
        if (!fam)
            return std::unexpected { lib::err::address_family_unsupported };

        return fam->create_pair(type, protocol);
    }

    auto create_anon(std::shared_ptr<socket_t> sock, int flags) -> lib::expect<int>
    {
        auto ret = create_anon_fd({
            .name = "<[SOCKET]>",
            .ops = ops::singleton(),
            .file_private_data = std::move(sock),
            .inode_private_data = nullptr,
            .st_mode = std::to_underlying(stat::s_ifsock) | s_irwxu | s_irwxg | s_irwxo,
            .flags = flags | o_rdwr,
            .skip_open = true,
            .inode = nullptr
        });
        if (!ret)
            return std::unexpected { ret.error() };

        return ret->first;
    }

    lib::initgraph::stage *registered_procfs_stage()
    {
        static lib::initgraph::stage stage
        {
            "socket.procfs.registered-net",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task net_task
    {
        "socket.procfs.register-net",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { fs::procfs::registered_stage() },
        lib::initgraph::entail { registered_procfs_stage() },
        [] {
            using namespace fs::procfs;
            lib::bug_on(!register_per_pid("net",
                make_dir_ops(),
                node_type::dir, 0555
            ));

            lib::bug_on(!register_global("net",
                make_symlink_ops([](auto) {
                    return std::string { "self/net" };
                }), node_type::symlink, 0777
            ));
        }
    };
} // namespace vfs::socket
