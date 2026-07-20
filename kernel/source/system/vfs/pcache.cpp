// Copyright (C) 2024-2026  ilobilo

module system.vfs;

namespace vfs
{
    namespace
    {
        struct file_object : vmm::object
        {
            sched::mutex_t lock;
            std::shared_ptr<file_t> backing;

            file_object(std::shared_ptr<file_t> file)
                : vmm::object { vmm::object_type::file }, backing { std::move(file) } { }

            std::shared_ptr<file_t> get_backing()
            {
                const std::unique_lock _ { lock };
                return backing;
            }

            void detach()
            {
                const std::unique_lock _ { lock };
                backing.reset();
            }

            private:
            lib::expect<void> fetch_pages(std::size_t idx, std::span<vmm::page *> pages) override
            {
                auto file = get_backing();
                if (!file)
                    return std::unexpected { lib::err::not_found };

                const auto psize = vmm::default_page_size();
                const auto npsize = vmm::pagemap::from_page_size(psize);

                for (std::size_t i = 0; i < pages.size(); i++)
                {
                    auto span = lib::maybe_uspan<std::byte>::create(
                        reinterpret_cast<std::byte *>(lib::tohh(vmm::paddr_from(pages[i]))),
                        npsize
                    );
                    lib::bug_on(!span.has_value());

                    const auto ret = file->pread((idx + i) * npsize, std::move(*span));
                    if (!ret.has_value())
                        return std::unexpected { ret.error() };
                }
                return { };
            }

            lib::expect<void> write_pages(std::size_t idx, std::span<vmm::page *> pages) override
            {
                auto file = get_backing();
                if (!file)
                    return std::unexpected { lib::err::not_found };

                const auto psize = vmm::default_page_size();
                const auto npsize = vmm::pagemap::from_page_size(psize);

                const auto &inode = file->path.dentry->inode;
                const std::size_t size = inode->stat.st_size;

                for (std::size_t i = 0; i < pages.size(); i++)
                {
                    const auto offset = (idx + i) * npsize;
                    if (offset >= size)
                        continue;

                    const auto len = std::min(npsize, size - offset);
                    auto span = lib::maybe_uspan<std::byte>::create(
                        reinterpret_cast<std::byte *>(lib::tohh(vmm::paddr_from(pages[i]))),
                        len
                    );
                    lib::bug_on(!span.has_value());

                    const auto ret = file->pwrite(offset, std::move(*span));
                    if (!ret.has_value())
                        return std::unexpected { ret.error() };
                    if (*ret != len)
                        return std::unexpected { lib::err::io_error };
                }
                return { };
            }
        };
    } // namespace

    lib::expect<vmm::object::ptr> ops_t::map(std::shared_ptr<file_t> file)
    {
        const auto &dentry = file->path.dentry;
        if (!dentry || !dentry->inode)
            return std::unexpected { lib::err::mapping_unsupported };

        auto &inode = dentry->inode;
        if (inode->stat.type() != stat::type::s_ifreg)
            return std::unexpected { lib::err::mapping_unsupported };

        const std::unique_lock _ { inode->lock };
        if (!inode->mapping)
        {
            auto backing = file_t::create(file->path, 0, 0);
            if (const auto ret = backing->open(0, 0); !ret.has_value())
                return std::unexpected { ret.error() };

            inode->mapping = new file_object { std::move(backing) };
        }
        return inode->mapping;
    }

    void inode_t::invalidate_pcache(std::uint64_t offset, std::size_t length)
    {
        if (length == 0)
            return;

        vmm::object::ptr obj;
        {
            const std::unique_lock _ { lock };
            obj = mapping;
        }
        if (!obj)
            return;

        const auto psize = vmm::default_page_size();
        const auto npsize = vmm::pagemap::from_page_size(psize);

        const auto startp = offset / npsize;
        const auto endp = lib::div_roundup(offset + length, npsize);

        obj->drop_cached(startp, endp - startp);
    }

    void inode_t::trunc_pcache(std::size_t size)
    {
        vmm::object::ptr obj;
        {
            const std::unique_lock _ { lock };
            obj = mapping;
        }
        if (!obj)
            return;

        const auto psize = vmm::default_page_size();
        const auto npsize = vmm::pagemap::from_page_size(psize);

        obj->drop_cached(size / npsize, ~0ul);
    }

    void inode_t::orphan_pcache()
    {
        vmm::object::ptr obj;
        {
            const std::unique_lock _ { lock };
            obj = std::move(mapping);
        }
        if (!obj)
            return;

        if (obj.use_count() > 1)
        {
            const auto psize = vmm::default_page_size();
            const auto npsize = vmm::pagemap::from_page_size(psize);

            const auto pages = lib::div_roundup(static_cast<std::size_t>(stat.st_size), npsize);
            if (const auto ret = obj->populate(pages); !ret.has_value())
            {
                lib::error(
                    "pcache: could not populate unlinked mapped file: {}",
                    lib::error_name(ret.error())
                );
            }
        }

        static_cast<file_object *>(obj.get())->detach();
    }
} // namespace vfs
