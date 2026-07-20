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
                mode = (mode & static_cast<mode_t>(stat::type::s_ifmt)) | s_irusr | s_iwusr;
                return { };
            }
        } cls;

        // TODO
        struct ctrl_ktype_t : dev::ktype_t
        {
            std::span<dev::attribute_t *const> attributes() const override
            {
                return { };
            }

            std::span<dev::bin_attribute_t *const> bin_attributes() const override
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
            std::span<dev::attribute_t *const> attributes() const override
            {
                return { };
            }

            std::span<dev::bin_attribute_t *const> bin_attributes() const override
            {
                return { };
            }

            void fill_uevent(dev::kobject_t &kobj, dev::uevent_t &uev) override
            {
                lib::unused(uev);

                auto dev = kobj.as_device();
                lib::bug_on(!dev);
            }
        };
    } // namespace

    // TODO
    lib::expect<std::size_t> ctrl_ops_t::read(
        std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        lib::unused(file, offset, buffer);
        auto ctrl = this->ctrl.lock();
        if (!ctrl)
            return std::unexpected { lib::err::invalid_device_or_address };
        return std::unexpected { lib::err::todo };
    }

    // TODO
    lib::expect<std::size_t> ctrl_ops_t::write(
        std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        lib::unused(file, offset, buffer);
        auto ctrl = this->ctrl.lock();
        if (!ctrl)
            return std::unexpected { lib::err::invalid_device_or_address };
        return std::unexpected { lib::err::todo };
    }

    // TODO
    lib::expect<int> ctrl_ops_t::ioctl(
        std::shared_ptr<vfs::file_t> file, std::uint64_t request,
        lib::uptr_or_addr argp
    )
    {
        lib::unused(file, request, argp);
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
} // namespace nvme
