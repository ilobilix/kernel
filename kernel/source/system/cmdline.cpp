// Copyright (C) 2024-2026  ilobilo

module system.cmdline;

import boot;

namespace cmdline
{
    namespace
    {
        std::optional<std::string> data;
        std::vector<std::pair<std::string_view, std::string_view>> cache;
    } // namespace

    std::string_view raw()
    {
        if (data)
            return *data;

        const auto resp = boot::requests::kernel_cmdline.response;
        if (resp == nullptr || resp->cmdline == nullptr)
            return { };
        return resp->cmdline;
    }

    std::optional<std::string_view> get(std::string_view req)
    {
        if (data)
        {
            for (const auto &[key, val] : cache)
            {
                if (key == req)
                    return val;
            }
            return std::nullopt;
        }

        for (const auto &[key, val] : lib::kvparse_view { raw(), ' ' })
        {
            if (key == req)
                return val;
        }
        return std::nullopt;
    }

    bool has(std::string_view req)
    {
        return get(req).has_value();
    }

    lib::initgraph::task cmdline_task
    {
        "cmdline.copy",
        lib::initgraph::postsched_init_engine,
        [] {
            data.emplace(raw());
            for (const auto &[key, val] : lib::kvparse_view{ *data, ' ' })
                cache.emplace_back(key, val);
        }
    };
} // namespace cmdline
