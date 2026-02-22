// Copyright (C) 2024-2026  ilobilo

export module lib:unused;

export namespace lib
{
    inline constexpr void unused([[maybe_unused]] auto &&...) { }
} // export namespace lib