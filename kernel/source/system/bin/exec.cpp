// Copyright (C) 2024-2026  ilobilo

module system.bin.exec;

namespace bin::exec
{
    namespace
    {
        // clang-format off
        lib::locker<
            lib::map::flat_hash<
                std::string_view,
                std::shared_ptr<format>
            >, lib::rwspinlock
        > formats;
        // clang-format on
    } // namespace

    bool register_format(std::shared_ptr<format> fmt)
    {
        auto [_, inserted] = formats.write_lock()->emplace(fmt->name(), fmt);
        if (inserted)
            lib::info("exec: registered format '{}'", fmt->name());
        return inserted;
    }

    std::shared_ptr<format> get_format(std::string_view name)
    {
        const auto rlocked = formats.read_lock();
        auto it = rlocked->find(name);
        if (it == rlocked->end())
            return nullptr;
        return it->second;
    }

    lib::expect<std::unique_ptr<image>> probe(
        const std::shared_ptr<vfs::file> &file, std::size_t depth
    )
    {
        if (depth >= max_depth)
            return std::unexpected { lib::err::binfmt_recursion };

        const auto rlocked = formats.read_lock();
        for (const auto &[name, fmt] : *rlocked)
        {
            auto ret = fmt->probe(file, depth + 1);
            if (ret.has_value())
            {
                if (*ret != nullptr)
                    return std::move(ret);
            }
            else
                return std::unexpected { ret.error() };
        }
        return std::unexpected { lib::err::invalid_binfmt };
    }
} // namespace bin::exec
