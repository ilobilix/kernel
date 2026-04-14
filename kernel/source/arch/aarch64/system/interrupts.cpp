// Copyright (C) 2024-2025  ilobilo

module system.interrupts;

import lib;
import std;

namespace interrupts
{
    std::optional<std::pair<handler &, std::size_t>> allocate(std::size_t cpuidx, std::size_t hint) { lib::unused(cpuidx, hint); return std::nullopt; }
    std::optional<std::reference_wrapper<handler>> get(std::size_t cpuidx, std::size_t vector) { lib::unused(cpuidx, vector); return std::nullopt; }
    void mask(std::size_t vector) { lib::unused(vector); }
    void unmask(std::size_t vector) { lib::unused(vector); }
} // export namespace interrupts