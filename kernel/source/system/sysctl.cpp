// Copyright (C) 2024-2026  ilobilo

module;

#include <version.h>

module system.sysctl;

import drivers.fs.procfs;
import system.sched;
import fmt;

namespace sysctl
{
    using namespace fs::procfs;

    bool register_entry(std::string_view path, read_fn read, write_fn write, mode_t mode)
    {
        return register_global(lib::path { "sys" } / path,
            make_file_ops(
                [read = std::move(read)](auto) -> lib::expect<std::string> {
                    return read();
                },
                [write = std::move(write)](auto, std::string_view data) -> lib::expect<void> {
                    return write(data);
                }
            ), node_type::file, mode
        );
    }

    bool register_ro(std::string_view path, read_fn read, mode_t mode)
    {
        return register_global(lib::path { "sys" } / path,
            make_file_ops(
                [read = std::move(read)](auto) -> lib::expect<std::string> {
                    return read();
                }, nullptr
            ), node_type::file, mode
        );
    }

    bool register_int(std::string_view path, int_read_fn read, int_write_fn write, mode_t mode)
    {
        return register_global(lib::path { "sys" } / path,
            make_file_ops(
                [read = std::move(read)](auto) -> lib::expect<std::string> {
                    return fmt::format("{}\n", read());
                },
                [write = std::move(write)](auto, std::string_view data) -> lib::expect<void> {
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
                }
            ), node_type::file, mode
        );
    }

    bool register_int_stub(std::string_view path, int default_value, mode_t mode)
    {
        auto storage = std::make_shared<std::atomic<int>>(default_value);
        return register_int(
            std::move(path),
            [storage] { return storage->load(std::memory_order_relaxed); },
            [storage](int val) -> lib::expect<void> {
                storage->store(val, std::memory_order_relaxed);
                return { };
            }, mode
        );
    }

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "sysctl.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task sysctl_task
    {
        "sysctl.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { fs::procfs::registered_stage() },
        lib::initgraph::entail { registered_stage() },
        [] {
            register_global("sys",
                make_dir_ops(),
                node_type::dir, 0555
            );

            lib::bug_on(!register_ro("kernel/ostype",
                [] { return std::string { "Ilobilix\n" }; }
            ));
            lib::bug_on(!register_ro("kernel/osrelease",
                [] { return std::string { ILOBILIX_RELEASE "\n" }; }
            ));
            lib::bug_on(!register_ro("kernel/version",
                [] { return std::string { __DATE__ " " __TIME__ "\n" }; }
            ));

            // TODO
            const auto checked = [](std::string_view path, int value) {
                lib::bug_on(!register_int_stub(path, value));
            };

            checked("kernel/random/poolsize", 256);
            checked("kernel/dmesg_restrict", 0);
            checked("kernel/kexec_load_disabled", 0);
            checked("kernel/yama/ptrace_scope", 0);
            checked("kernel/core_uses_pid", 0);
            checked("kernel/kptr_restrict", 0);
            checked("kernel/perf_event_paranoid", 4);
            checked("kernel/unprivileged_bpf_disabled", 0);
            checked("fs/protected_hardlinks", 1);
            checked("fs/protected_symlinks", 1);
        }
    };
} // namespace sysctl
