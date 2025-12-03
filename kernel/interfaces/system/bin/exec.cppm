// Copyright (C) 2024-2025  ilobilo

export module system.bin.exec;

import system.memory.virt;
import system.scheduler;
import system.vfs;
import std;

export namespace bin::exec
{
    struct request
    {
        std::string pathname;
        std::shared_ptr<vfs::file> file;
        std::shared_ptr<vfs::file> interp;

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

        virtual bool identify(const std::shared_ptr<vfs::file> &file) const = 0;
        virtual sched::thread *load(const request &req, sched::process *proc) const = 0;

        std::string_view name() const { return _name; }
    };

    bool register_format(std::shared_ptr<format> fmt);
    std::shared_ptr<format> get_format(std::string_view name);

    std::shared_ptr<format> identify(const std::shared_ptr<vfs::file> &file);
} // export namespace bin::exec