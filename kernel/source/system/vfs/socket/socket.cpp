// Copyright (C) 2024-2026  ilobilo

module system.vfs.socket;

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

        struct ops_t : vfs::ops_t
        {
            static std::shared_ptr<ops_t> singleton()
            {
                static auto instance = std::make_shared<ops_t>();
                return instance;
            }

            bool seekable() const override { return false; }

            lib::expect<void> open(const std::shared_ptr<vfs::file_t> &file, int flags, pid_t pid) override
            {
                lib::unused(file, flags, pid);
                return std::unexpected { lib::err::invalid_device_or_address };
            }

            lib::expect<void> close(vfs::file_t &file) override
            {
                return from_file(file)->release();
            }

            lib::expect<std::size_t> read(
                const std::shared_ptr<vfs::file_t> &file, std::uint64_t offset,
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
                const std::shared_ptr<vfs::file_t> &file, std::uint64_t offset,
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


            lib::expect<std::uint16_t> poll(
                const std::shared_ptr<vfs::file_t> &file, vfs::poll_table_t *pt
            ) override
            {
                return from_file(*file)->poll(pt);
            }

            lib::expect<int> ioctl(
                const std::shared_ptr<file_t> &file, std::uint64_t request,
                lib::uptr_or_addr argp
            ) override
            {
                return from_file(*file)->ioctl(request, argp);
            }
        };
    } // namespace

    lib::expect<bool> msg_header_t::write_cmsg(int type, std::span<const std::byte> data)
    {
        const auto total = sizeof(cmsghdr) + data.size();
        const auto stride = (total + cmsg_align - 1) & ~(cmsg_align - 1);

        if (msgctrl_len_out + total > msgctrl.size())
        {
            out_flags |= msg_ctrunc;
            return false;
        }

        const cmsghdr hd {
            .len = total,
            .level = sol_socket,
            .type = type
        };
        if (!msgctrl.subspan(msgctrl_len_out, sizeof(cmsghdr))
            .copy_from(std::as_bytes(std::span { &hd, 1 })))
            return std::unexpected { lib::err::invalid_address };
        if (!msgctrl.subspan(msgctrl_len_out + sizeof(cmsghdr), data.size())
            .copy_from(data))
            return std::unexpected { lib::err::invalid_address };

        msgctrl_len_out = std::min(msgctrl_len_out + stride, msgctrl.size());
        return true;
    }

    auto socket_t::setsockopt(sock_lvl lvl, int opt, lib::maybe_uspan<const std::byte> buf)
        -> lib::expect<void>
    {
        if (lvl != sol_socket)
            return do_setsockopt(lvl, opt, buf);

        auto locked = core_state.lock();
        const auto store = [&]<typename Type>(Type core_state_t::*member) {
            std::conditional_t<std::is_class_v<Type>, Type, int> val;
            return buf.copy_val_to(val).transform([&] {
                if constexpr (std::is_same_v<Type, std::size_t>)
                    val = std::max(val, 0);
                (*locked).*member = val;
            });
        };

        auto sopt = static_cast<sock_opt>(opt);
        lib::expect<void> ret { };

        switch (sopt)
        {
            // TODO
            // case so_debug:
            //     break;
            case so_reuseaddr:
                ret = store(&core_state_t::reuseaddr);
                break;
            case so_dontroute:
                ret = store(&core_state_t::dontroute);
                break;
            case so_broadcast:
                ret = store(&core_state_t::broadcast);
                break;
            case so_sndbuf:
                ret = store(&core_state_t::sndbuf);
                break;
            case so_rcvbuf:
                ret = store(&core_state_t::rcvbuf);
                break;
            case so_keepalive:
                ret = store(&core_state_t::keepalive);
                break;
            // TODO
            // case so_oobinline:
            //     break;
            case so_linger:
                ret = store(&core_state_t::linger_opt);
                break;
            case so_reuseport:
                ret = store(&core_state_t::reuseport);
                break;
            case so_passcred:
                ret = store(&core_state_t::passcred);
                break;
            case so_rcvtimeo:
            case so_sndtimeo:
            {
                timeval val;
                if (const auto res = buf.copy_val_to(val); !res)
                    return res;

                if (val.tv_sec < 0)
                    val = { };
                if (!val.valid())
                    return std::unexpected { lib::err::out_of_domain };

                if (sopt == so_rcvtimeo)
                    locked->rcvtimeo = val;
                else
                    locked->sndtimeo = val;
                break;
            }
            // TODO
            case so_attach_filter:
            case so_detach_filter:
            case so_lock_filter:
                break;
            case so_sndbufforce:
                if (!sched::capable(sched::cap_t::net_admin))
                    return std::unexpected { lib::err::not_permitted };
                ret = store(&core_state_t::sndbuf);
                sopt = so_sndbuf;
                break;
            case so_rcvbufforce:
                if (!sched::capable(sched::cap_t::net_admin))
                    return std::unexpected { lib::err::not_permitted };
                ret = store(&core_state_t::rcvbuf);
                sopt = so_rcvbuf;
                break;
            case so_type:
            case so_error:
            case so_peercred:
            case so_acceptconn:
            case so_protocol:
            case so_domain:
            default:
                return std::unexpected { lib::err::protocol_not_available };
        }

        if (!ret)
            return ret;

        sol_sock_finish(*locked, sopt);
        return { };
    }

    auto socket_t::getsockopt(sock_lvl lvl, int opt, lib::maybe_uspan<std::byte> buf)
        -> lib::expect<std::size_t>
    {
        if (lvl != sol_socket)
            return do_getsockopt(lvl, opt, buf);

        switch (static_cast<sock_opt>(opt))
        {
            // TODO
            // case so_debug:
            //     break;
            case so_reuseaddr:
                return buf.copy_val_from<int>(core_state.lock()->reuseaddr);
            case so_type:
                return buf.copy_val_from<int>(type);
            case so_error:
                return buf.copy_val_from<int>(
                    pending_err.exchange(errnos { }, std::memory_order_acq_rel)
                );
            case so_dontroute:
                return buf.copy_val_from<int>(core_state.lock()->dontroute);
            case so_broadcast:
                return buf.copy_val_from<int>(core_state.lock()->broadcast);
            case so_sndbufforce:
            case so_sndbuf:
                return buf.copy_val_from<int>(core_state.lock()->sndbuf);
            case so_rcvbufforce:
            case so_rcvbuf:
                return buf.copy_val_from<int>(core_state.lock()->rcvbuf);
            case so_keepalive:
                return buf.copy_val_from<int>(core_state.lock()->keepalive);
            // TODO
            // case so_oobinline:
            //     break;
            case so_linger:
                return buf.copy_val_from(core_state.lock()->linger_opt);
            case so_reuseport:
                return buf.copy_val_from<int>(core_state.lock()->reuseport);
            case so_passcred:
                return buf.copy_val_from<int>(core_state.lock()->passcred);
            case so_rcvtimeo:
                return buf.copy_val_from(core_state.lock()->rcvtimeo);
            case so_sndtimeo:
                return buf.copy_val_from(core_state.lock()->sndtimeo);
            // TODO
            // case so_attach_filter:
            //     break;
            // TODO
            // case so_detach_filter:
            //     break;
            case so_acceptconn:
                return buf.copy_val_from<int>(core_state.lock()->state == listening);
            case so_protocol:
                return buf.copy_val_from<int>(protocol);
            case so_domain:
                return buf.copy_val_from<int>(family);
            // TODO
            // case so_lock_filter:
            //     break;
            case so_peercred:
                return buf.copy_val_from(core_state.lock()->peer_cred.value_or(
                    ucred { .pid = 0, .uid = unset_id, .gid = unset_id }
                ));
            default:
                break;
        }
        return do_getsockopt(lvl, opt, buf);
    }

    std::shared_ptr<vfs::ops_t> sock_ops()
    {
        return ops_t::singleton();
    }

    std::shared_ptr<socket_t> from_file(const vfs::file_t &file)
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
            .name = "socket",
            .ops = ops_t::singleton(),
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
} // namespace vfs::socket
