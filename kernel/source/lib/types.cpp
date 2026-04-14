// Copyright (C) 2024-2025  ilobilo

module lib;

import system.time;
import cppstd;

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
        copy_from_user(str.data(), ustr, length);
    }
} // namespace lib

void stat::update_time(std::uint8_t flags)
{
    if (flags & time::access)
        st_atim = ::time::now();
    if (flags & time::modify)
        st_mtim = ::time::now();
    if (flags & time::status)
        st_ctim = ::time::now();
}