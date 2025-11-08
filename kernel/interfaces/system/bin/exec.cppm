// Copyright (C) 2024-2025  ilobilo

export module system.bin.exec;

import system.memory.virt;
import system.scheduler;
import system.vfs;
import cppstd;

export namespace bin::exec
{
    struct request
    {
        vfs::path file;
        std::optional<vfs::path> interp;

        std::vector<std::string> argv;
        std::vector<std::string> envp;
    };

    class format
    {
        private:
        std::string _name;

        public:
        format(std::string_view name) : _name { name } { }

        virtual ~format() = default;

        virtual bool identify(const std::shared_ptr<vfs::dentry> &file) = 0;
        virtual sched::thread *load(request &req, sched::process *proc) = 0;

        std::string_view name() const { return _name; }
    };

    bool register_format(std::shared_ptr<format> fmt);
    std::shared_ptr<format> get_format(std::string_view name);

    std::shared_ptr<format> identify(const std::shared_ptr<vfs::dentry> &file);
} // export namespace bin::exec