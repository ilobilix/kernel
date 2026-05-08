// Copyright (C) 2024-2026  ilobilo

module;

#include <version.h>

module system.sysctl;

import system.sched.mutex;
import fmt;

namespace sysctl
{
    namespace
    {
        struct path_less
        {
            using is_transparent = void;
            bool operator()(std::string_view lhs, std::string_view rhs) const
            {
                return lhs.compare(rhs) < 0;
            }
        };

        lib::locker<lib::btree::map<std::string, entry, path_less>, sched::mutex> registry;

        std::string_view first_segment(std::string_view path)
        {
            const auto slash = path.find('/');
            if (slash == std::string_view::npos)
                return path;
            return path.substr(0, slash);
        }

        bool starts_under(std::string_view path, std::string_view dir)
        {
            if (dir.empty())
                return true;
            if (!path.starts_with(dir))
                return false;
            return path.size() > dir.size() && path[dir.size()] == '/';
        }
    } // namespace

    bool register_entry(std::string path, read_fn read, write_fn write, mode_t mode)
    {
        if (path.empty() || path.front() == '/' || path.back() == '/')
            return false;
        if (path.find("//") != std::string::npos)
            return false;

        auto locked = registry.lock();
        return locked->emplace(
            std::move(path),
            entry { std::move(read), std::move(write), mode }
        ).second;
    }

    bool register_ro(std::string path, read_fn read, mode_t mode)
    {
        return register_entry(std::move(path), std::move(read), nullptr, mode);
    }

    bool register_int(std::string path, int_read_fn read, int_write_fn write, mode_t mode)
    {
        return register_entry(
            std::move(path),
            [read = std::move(read)] {
                return fmt::format("{}\n", read());
            },
            [write = std::move(write)](std::string_view data) -> lib::expect<void> {
                while (!data.empty() && (data.front() == ' ' || data.front() == '\t'))
                    data.remove_prefix(1);

                bool neg = false;
                if (!data.empty() && (data.front() == '-' || data.front() == '+'))
                {
                    neg = data.front() == '-';
                    data.remove_prefix(1);
                }
                if (data.empty() || data.front() < '0' || data.front() > '9')
                    return std::unexpected { lib::err::invalid_argument };

                int val = 0;
                while (!data.empty() && data.front() >= '0' && data.front() <= '9')
                {
                    val = val * 10 + (data.front() - '0');
                    data.remove_prefix(1);
                }
                return write(neg ? -val : val);
            },
            mode
        );
    }

    bool register_int_stub(std::string path, int default_value, mode_t mode)
    {
        auto storage = std::make_shared<std::atomic<int>>(default_value);
        return register_int(
            std::move(path),
            [storage] { return storage->load(std::memory_order_relaxed); },
            [storage] (int val) -> lib::expect<void> {
                storage->store(val, std::memory_order_relaxed);
                return { };
            },
            mode
        );
    }

    std::optional<entry> find(std::string_view path)
    {
        const auto locked = registry.lock();
        auto it = locked->find(path);
        if (it == locked->end())
            return std::nullopt;
        return it->second;
    }

    std::vector<dirent> list(std::string_view dir)
    {
        std::vector<dirent> out;
        const auto locked = registry.lock();
        for (const auto &[full, _] : *locked)
        {
            const std::string_view path { full };
            if (!starts_under(path, dir))
                continue;

            const auto rest = path.substr(dir.empty() ? 0 : dir.size() + 1);
            const auto seg = first_segment(rest);
            const bool is_dir = seg.size() < rest.size();

            if (!out.empty() && out.back().name.compare(seg) == 0)
                continue;
            out.push_back({ std::string { seg }, is_dir });
        }
        return out;
    }

    bool dir_exists(std::string_view path)
    {
        if (path.empty())
            return true;
        const auto locked = registry.lock();
        for (const auto &[full, _] : *locked)
        {
            if (starts_under(std::string_view { full }, path))
                return true;
        }
        return false;
    }

    lib::initgraph::task sysctl_register_task
    {
        "sysctl.register",
        lib::initgraph::postsched_init_engine,
        [] {
            sysctl::register_ro("kernel/ostype",
                [] { return std::string { "Ilobilix\n" }; }
            );
            sysctl::register_ro("kernel/osrelease",
                [] { return std::string { ILOBILIX_RELEASE "\n" }; }
            );
            sysctl::register_ro("kernel/version",
                [] { return std::string { __DATE__ " " __TIME__ "\n" }; }
            );

            // TODO
            sysctl::register_int_stub("kernel/random/poolsize", 256);
            sysctl::register_int_stub("kernel/dmesg_restrict", 0);
            sysctl::register_int_stub("kernel/kexec_load_disabled", 0);
            sysctl::register_int_stub("kernel/yama/ptrace_scope", 0);
            sysctl::register_int_stub("kernel/core_uses_pid", 0);
            sysctl::register_int_stub("kernel/kptr_restrict", 0);
            sysctl::register_int_stub("kernel/perf_event_paranoid", 4);
            sysctl::register_int_stub("kernel/unprivileged_bpf_disabled", 0);
            sysctl::register_int_stub("fs/protected_hardlinks", 1);
            sysctl::register_int_stub("fs/protected_symlinks", 1);
        }
    };
} // namespace sysctl
