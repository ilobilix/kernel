// Copyright (C) 2024-2025  ilobilo

module system.bin.exec;

import lib;
import std;

namespace bin::script
{
    class format : public exec::format
    {
        public:
        format() : exec::format { "script" } { }

        bool identify(const std::shared_ptr<vfs::file> &file) const override
        {
            lib::unused(file);
            return false;
        }

        sched::thread *load(const exec::request &req,  sched::process *proc) const override
        {
            lib::unused(req, proc);
            return nullptr;
        }
    };

    lib::initgraph::task script_exec_task
    {
        "bin.exec.script.register",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { lib::initgraph::base_stage() },
        [] { exec::register_format(std::make_shared<format>()); }
    };
} // namespace bin::script