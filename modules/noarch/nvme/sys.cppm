// Copyright (C) 2024-2026  ilobilo

export module nvme:sys;

import system.dev;
import system.vfs;

import :ctrl;
import :ns;

export namespace nvme
{
    struct ctrl_ops_t : vfs::ops
    {
        std::weak_ptr<controller_t> ctrl;

        ctrl_ops_t(std::weak_ptr<controller_t> ctrl)
            : vfs::ops { }, ctrl { std::move(ctrl) } { }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<int> ioctl(
            std::shared_ptr<vfs::file> file, std::uint64_t request,
            lib::uptr_or_addr argp
        ) override;
    };

    dev::class_t &get_class();
    dev::ktype_t &get_ctrl_ktype();
    dev::ktype_t &get_ns_ktype();
} // export namespace nvme
