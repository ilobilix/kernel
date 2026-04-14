// Copyright (C) 2024-2025  ilobilo

export module lib:containers;
import std;

export namespace lib
{
    inline constexpr bool erase_from(auto &container, const auto &val)
    {
        const auto end = std::ranges::end(container);
        return container.erase(std::remove(std::ranges::begin(container), end, val), end) != end;
    }

    inline constexpr bool erase_from_if(auto &container, auto pred)
    {
        const auto end = std::ranges::end(container);
        return container.erase(std::remove_if(std::ranges::begin(container), end, pred), end) != end;
    }
} // export namespace lib