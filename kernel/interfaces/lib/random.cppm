// Copyright (C) 2024-2025  ilobilo

export module lib:random;

import :user;
import std;

export namespace lib
{
    std::ssize_t random_bytes(lib::maybe_uspan<std::byte> buffer);
} // export namespace lib