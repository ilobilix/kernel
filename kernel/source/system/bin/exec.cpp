// Copyright (C) 2024-2025  ilobilo

module system.bin.exec;

import system.vfs;
import lib;
import std;

namespace bin::exec
{
    namespace
    {
        lib::locker<
            lib::map::flat_hash<
                std::string_view,
                std::shared_ptr<format>
            >, lib::rwspinlock
        > formats;
    } // namespace

    bool register_format(std::shared_ptr<format> fmt)
    {
        auto [_, inserted] = formats.write_lock()->emplace(fmt->name(), fmt);
        if (inserted)
            log::info("exec: registered format '{}'", fmt->name());
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

    std::shared_ptr<format> identify(const std::shared_ptr<vfs::file> &file)
    {
        const auto rlocked = formats.read_lock();
        for (const auto &[name, fmt] : *rlocked)
        {
            if (fmt->identify(file))
                return fmt;
        }
        return nullptr;
    }
} // namespace bin::exec