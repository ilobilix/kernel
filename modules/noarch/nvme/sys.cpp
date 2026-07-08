// Copyright (C) 2024-2026  ilobilo

module nvme;

import lib;

import :sys;

namespace nvme
{
    namespace
    {
        struct class_t : dev::class_t
        {
            class_t() : dev::class_t { "nvme", dev::empty_ktype(), false } { }

            std::string devnode(const dev::device_t &dev, mode_t &mode) const
            {
                lib::unused(dev);
                mode = 0600;
                return { };
            }
        } cls;

        // TODO
        struct ctrl_ktype_t : dev::ktype_t
        {
            std::span<dev::attribute_t> attributes() const override
            {
                return { };
            }

            std::span<dev::bin_attribute_t> bin_attributes() const override
            {
                return { };
            }

            void fill_uevent(dev::kobject_t &kobj, dev::uevent_t &uev) override
            {
                auto dev = kobj.as_device();
                lib::bug_on(!dev);

                // TODO: other types and keys
                uev.add("NVME_TRTYPE", "pcie");
            }
        };

        // TODO
        struct ns_ktype_t : dev::ktype_t
        {
            std::span<dev::attribute_t> attributes() const override
            {
                return { };
            }

            std::span<dev::bin_attribute_t> bin_attributes() const override
            {
                return { };
            }

            void fill_uevent(dev::kobject_t &kobj, dev::uevent_t &uev) override
            {
                auto dev = kobj.as_device();
                lib::bug_on(!dev);
            }
        };
    } // namespace

    // TODO
    lib::expect<std::size_t> ns_ops_t::read(
        std::shared_ptr<vfs::file> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        // o_direct bypasses page cache
        // o_sync waits for write
        auto ns = this->ns.lock();
        if (!ns)
            return std::unexpected { lib::err::invalid_device_or_address };

        if (offset >= ns->size_bytes())
            return 0;
        if (buffer.size_bytes() > ns->size_bytes() - offset)
            buffer = buffer.subspan(0, ns->size_bytes() - offset);

        if (const auto ret = ns->write(offset, buffer); !ret)
            return std::unexpected { ret.error() };
        return buffer.size();
    }

    // TODO
    lib::expect<std::size_t> ns_ops_t::write(
        std::shared_ptr<vfs::file> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        auto ns = this->ns.lock();
        if (!ns)
            return std::unexpected { lib::err::invalid_device_or_address };

        if (offset >= ns->size_bytes())
            return std::unexpected { lib::err::no_space_left };
        if (buffer.size_bytes() > ns->size_bytes() - offset)
            buffer = buffer.subspan(0, ns->size_bytes() - offset);

        if (const auto ret = ns->write(offset, buffer); !ret)
            return std::unexpected { ret.error() };
        return buffer.size();
    }

    // TODO
    lib::expect<std::size_t> ctrl_ops_t::read(
        std::shared_ptr<vfs::file> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        auto ctrl = this->ctrl.lock();
        if (!ctrl)
            return std::unexpected { lib::err::invalid_device_or_address };
        return std::unexpected { lib::err::todo };
    }

    // TODO
    lib::expect<std::size_t> ctrl_ops_t::write(
        std::shared_ptr<vfs::file> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        auto ctrl = this->ctrl.lock();
        if (!ctrl)
            return std::unexpected { lib::err::invalid_device_or_address };
        return std::unexpected { lib::err::todo };
    }

    lib::expect<int> ctrl_ops_t::ioctl(
        std::shared_ptr<vfs::file> file, std::uint64_t request,
        lib::uptr_or_addr argp
    )
    {
        auto ctrl = this->ctrl.lock();
        if (!ctrl)
            return std::unexpected { lib::err::invalid_device_or_address };
        return std::unexpected { lib::err::inappropriate_ioctl };
    }

    dev::class_t &get_class()
    {
        static class_t cls { };
        return cls;
    }

    dev::ktype_t &get_ctrl_ktype()
    {
        static ctrl_ktype_t type { };
        return type;
    }

    dev::ktype_t &get_ns_ktype()
    {
        static ns_ktype_t type { };
        return type;
    }
} // namespace nvme
