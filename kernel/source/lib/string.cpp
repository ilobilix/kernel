// Copyright (C) 2024-2025  ilobilo

module lib;

import system.chrono;
import std;

namespace lib
{
    user_string::user_string(const char __user *ustr)
    {
        static constexpr std::size_t max_string_length = 4096;
        if (ustr == nullptr)
        {
            str = "";
            return;
        }

        const auto length = strnlen_user(ustr, max_string_length);
        if (length == 0 || length == max_string_length)
        {
            str = "";
            return;
        }

        str.resize(length);
        if (!copy_from_user(str.data(), ustr, length))
            str = "";
    }
} // namespace lib