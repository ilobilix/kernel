// Copyright (C) 2024-2026  ilobilo

module system.bin.exec;

import system.sched.mutex;
import system.rcu;

namespace bin::exec
{
    namespace
    {
        using formats_t = rcu::box<
            lib::map::flat_hash<
                std::string_view,
                std::shared_ptr<format>
            >
        >;
        rcu::pointer<formats_t> formats;
        sched::mutex_t write_lock;
    } // namespace

    bool register_format(std::shared_ptr<format> fmt)
    {
        const std::unique_lock _ { write_lock };

        const auto name = fmt->name();

        rcu::updater next { formats };
        if (!next->emplace(name, std::move(fmt)).second)
            return false;
        next.commit();

        lib::info("exec: registered format '{}'", name);
        return true;
    }

    std::shared_ptr<format> get_format(std::string_view name)
    {
        const rcu::read_guard _ { };

        const auto *table = formats.dereference();
        if (!table)
            return nullptr;

        auto it = table->find(name);
        if (it == table->end())
            return nullptr;
        return it->second;
    }

    lib::expect<std::unique_ptr<image>> probe(
        const std::shared_ptr<vfs::file_t> &file, std::size_t depth
    )
    {
        if (depth >= max_depth)
            return std::unexpected { lib::err::symloop_max };

        std::vector<std::shared_ptr<format>> candidates;
        {
            const rcu::read_guard _ { };
            if (const auto *table = formats.dereference())
            {
                candidates.reserve(table->size());
                for (const auto &[_, fmt] : *table)
                    candidates.push_back(fmt);
            }
        }

        for (const auto &fmt : candidates)
        {
            auto ret = fmt->probe(file, depth + 1);
            if (ret.has_value())
            {
                if (*ret != nullptr)
                    return std::move(ret);
            }
            else return std::unexpected { ret.error() };
        }
        return std::unexpected { lib::err::invalid_exec };
    }
} // namespace bin::exec
