// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.procfs;

import system.sched;
import lib;
import std;

export namespace fs::procfs
{
    using gen_fn = std::string (*)(sched::process_t *);

    // /proc/<name>
    bool register_global(std::string name, gen_fn gen, mode_t mode, bool is_symlink = false);

    // /proc/[pid]/<name>
    bool register_per_pid(std::string name, gen_fn gen, mode_t mode, bool is_symlink = false);

    lib::initgraph::stage *registered_stage();
    lib::initgraph::stage *mounted_stage();
} // export namespace fs::procfs
