// Copyright (C) 2024-2025  ilobilo

export module lib:containers;
import cppstd;

export namespace lib
{
    inline constexpr bool erase_from(auto &container, const auto &val)
    {
        return container.erase(std::remove(std::ranges::begin(container), std::ranges::end(container), val), std::ranges::end(container)) != std::ranges::end(container);
    }

    inline constexpr bool erase_from_if(auto &container, auto pred)
    {
        return container.erase(std::remove_if(std::ranges::begin(container), std::ranges::end(container), pred), std::ranges::end(container)) != std::ranges::end(container);
    }
} // export namespace lib