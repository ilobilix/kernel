// Copyright (C) 2024-2026  ilobilo

module lib;

import system.chrono;
import std;

namespace lib
{
    std::optional<std::string> user_string::get(const char __user *ustr, std::size_t max_length)
    {
        if (ustr == nullptr)
            return std::nullopt;

        const auto length = strnlen_user(ustr, max_length);
        if (length <= 0 || static_cast<std::size_t>(length) == max_length)
            return std::nullopt;

        std::string ret = "";
        ret.resize(length);
        if (!copy_from_user(ret.data(), ustr, length))
            return std::nullopt;
        return ret;
    }
} // namespace lib
