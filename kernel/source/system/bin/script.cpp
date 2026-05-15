// Copyright (C) 2024-2026  ilobilo

module system.bin.exec;

namespace bin::script
{
    namespace
    {
        constexpr std::string_view fmt_name { "script" };
        constexpr std::size_t max_length = 256;
    } // namespace

    class image : public exec::image
    {
        private:
        std::unique_ptr<exec::image> _interp;
        std::string _interp_path;
        std::optional<std::string> _interp_arg;

        public:
        image(
            const std::shared_ptr<vfs::file> &file, std::unique_ptr<exec::image> interp,
            const std::string_view &interp_path, std::optional<std::string> interp_arg
        ) : exec::image { std::move(file) }, _interp { std::move(interp) },
            _interp_path { interp_path }, _interp_arg { std::move(interp_arg) } { }

        std::shared_ptr<sched::thread_t> load(const exec::request &req) const override
        {
            exec::request new_req {
                .pathname = req.pathname.empty()
                    ? vfs::pathname_from(file->path)
                    : req.pathname,
                .argv = { },
                .envp = req.envp,
                .proc = req.proc,
            };

            new_req.argv.reserve(req.argv.size() + (_interp_arg ? 3 : 2));
            new_req.argv.push_back(_interp_path);
            if (_interp_arg)
                new_req.argv.push_back(*_interp_arg);
            new_req.argv.push_back(new_req.pathname);
            for (std::size_t i = 1; i < req.argv.size(); i++)
                new_req.argv.push_back(req.argv[i]);

            return _interp->load(new_req);
        }

        std::string_view format_name() const override { return fmt_name; }
    };

    class format : public exec::format
    {
        public:
        format() : exec::format { fmt_name } { }

        lib::expect<std::unique_ptr<exec::image>> probe(
            const std::shared_ptr<vfs::file> &file, std::size_t depth
        ) const override
        {
            lib::membuffer data { max_length };
            auto uspan = data.maybe_uspan();
            lib::bug_on(!uspan);

            const auto ret = file->pread(0, *uspan);
            if (!ret)
                return nullptr;

            const std::size_t length = *ret;
            if (length < 3)
                return nullptr;

            auto buf = reinterpret_cast<char *>(data.data());
            auto end = buf + length;

            if (buf[0] != '#' || buf[1] != '!')
                return nullptr;
            buf += 2;

            auto line_end = buf;
            while (line_end != end && *line_end != '\n' && *line_end != '\0')
                line_end++;
            end = line_end;

            if (end != buf && end[-1] == '\r')
                end--;

            while (buf != end && (*buf == ' ' || *buf == '\t'))
                buf++;
            while (end != buf && (end[-1] == ' ' || end[-1] == '\t'))
                end--;
            if (buf == end)
                return std::unexpected { lib::err::invalid_binfmt };

            const auto istart = buf;
            while (buf != end && *buf != ' ' && *buf != '\t')
                buf++;
            const std::string_view path { istart, buf };

            while (buf != end && (*buf == ' ' || *buf == '\t'))
                buf++;
            const std::string_view arg { buf, end };

            // TODO: relative paths?
            if (lib::path_view { path } .is_absolute() == false)
                return std::unexpected { lib::err::invalid_binfmt };

            auto rret = vfs::resolve(file->path, path);
            if (!rret.has_value())
                return std::unexpected { lib::err::invalid_binfmt };

            auto res = vfs::reduce(std::move(rret->parent), std::move(rret->target));
            if (!res.has_value())
                return std::unexpected { lib::err::invalid_binfmt };

            auto interp_file = vfs::file::create(std::move(*res), 0, 0);
            auto next = exec::probe(interp_file, depth);
            if (!next.has_value())
                return std::unexpected { next.error() };
            if (*next == nullptr)
                return std::unexpected { lib::err::invalid_binfmt };

            return std::make_unique<image>(
                file, std::move(*next), path,
                arg.empty() ? std::nullopt : std::optional<std::string> { arg }
            );
        }
    };

    lib::initgraph::task script_exec_task
    {
        "bin.exec.script.register",
        lib::initgraph::postsched_init_engine,
        [] {
            exec::register_format(std::make_shared<format>());
        }
    };
} // namespace bin::script
