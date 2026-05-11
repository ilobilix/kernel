// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.procfs;

import system.sched;
import lib;
import std;

export namespace fs::procfs
{
    enum class node_type { file, dir, symlink };

    struct node_ops;
    struct node_t
    {
        std::string name;
        mode_t mode;
        node_type type;
        std::shared_ptr<node_ops> ops;
    };

    struct node_ops
    {
        virtual ~node_ops() = default;

        virtual bool can_trunc() { return false; }

        virtual lib::expect<std::string> generate(sched::process_t *proc)
        {
            lib::unused(proc);
            return std::unexpected { lib::err::not_supported };
        }

        virtual lib::expect<void> write(sched::process_t *proc, std::string_view data)
        {
            lib::unused(proc, data);
            return std::unexpected { lib::err::read_only_fs };
        }

        virtual lib::expect<lib::path> readlink(sched::process_t *proc)
        {
            lib::unused(proc);
            return std::unexpected { lib::err::not_supported };
        }

        virtual lib::expect<lib::list<node_t>> readdir(sched::process_t *proc)
        {
            lib::unused(proc);
            return std::unexpected { lib::err::not_supported };
        }

        virtual lib::expect<node_t> lookup(sched::process_t *proc, std::string_view name)
        {
            lib::unused(proc, name);
            return std::unexpected { lib::err::not_supported };
        }

        virtual bool revalidate(sched::process_t *proc)
        {
            lib::unused(proc);
            return true;
        }
    };

    // I hate myself
    using gen_fn = std::function<lib::expect<std::string> (sched::process_t *)>;
    using write_fn = std::function<lib::expect<void> (sched::process_t *, std::string_view)>;
    using readlink_fn = std::function<lib::expect<lib::path> (sched::process_t *)>;
    using lookup_fn = std::function<lib::expect<node_t> (sched::process_t *, std::string_view)>;
    using readdir_fn = std::function<lib::expect<lib::list<node_t>> (sched::process_t *)>;
    using revalidate_fn = std::function<bool (sched::process_t *)>;

    std::shared_ptr<node_ops> make_file_ops(gen_fn gfn, write_fn wfn = nullptr);
    std::shared_ptr<node_ops> make_symlink_ops(readlink_fn rdlfn, revalidate_fn rvfn = nullptr);

    std::shared_ptr<node_ops> make_dir_ops(lookup_fn lfn = nullptr, readdir_fn rfn = nullptr);

    // /proc/<path>
    bool register_global(
        lib::path path, std::shared_ptr<node_ops> ops,
        node_type type, mode_t mode
    );

    // /proc/[pid]/<path>
    bool register_per_pid(
        lib::path path, std::shared_ptr<node_ops> ops,
        node_type type, mode_t mode
    );

    lib::initgraph::stage *registered_stage();
    lib::initgraph::stage *mounted_stage();
} // export namespace fs::procfs
