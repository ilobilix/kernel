// Copyright (C) 2024-2026  ilobilo

export module system.bin.exec;

import system.memory.virt;
import system.sched;
import system.vfs;
import lib;
import std;

export namespace bin::exec
{
    constexpr std::size_t max_depth = 4;

    struct request
    {
        std::string pathname;
        std::vector<std::string> argv;
        std::vector<std::string> envp;
        sched::process_t *proc;
    };

    class image
    {
        protected:
        std::shared_ptr<vfs::file> file;

        image(std::shared_ptr<vfs::file> f) : file { std::move(f) } { }

        public:
        virtual ~image() = default;

        virtual sched::thread_t *load(const request &req) const = 0;

        virtual std::string_view format_name() const = 0;
    };

    class format
    {
        private:
        std::string _name;

        public:
        format(std::string_view name) : _name { name } { }
        virtual ~format() = default;

        virtual lib::expect<std::unique_ptr<image>> probe(
            const std::shared_ptr<vfs::file> &file, std::size_t depth = 0
        ) const = 0;

        std::string_view name() const { return _name; }
    };

    bool register_format(std::shared_ptr<format> fmt);
    std::shared_ptr<format> get_format(std::string_view name);

    lib::expect<std::unique_ptr<image>> probe(
        const std::shared_ptr<vfs::file> &file, std::size_t depth = 0
    );
} // export namespace bin::exec
