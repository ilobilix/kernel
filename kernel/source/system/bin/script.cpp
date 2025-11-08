// Copyright (C) 2024-2025  ilobilo

module system.bin.exec;

import lib;
import cppstd;

namespace bin::script
{
    class format : public exec::format
    {
        public:
        format() : exec::format { "script" } { }

        bool identify(const std::shared_ptr<vfs::dentry> &file) override
        {
            lib::unused(file);
            return false;
        }

        sched::thread *load(exec::request &req,  sched::process *proc) override
        {
            lib::unused(req, proc);
            return nullptr;
        }
    };

    initgraph::task script_exec_task
    {
        "exec.register-script",
        [] { exec::register_format(std::make_shared<format>()); }
    };
} // namespace bin::script