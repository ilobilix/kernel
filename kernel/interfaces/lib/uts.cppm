// Copyright (C) 2024-2026  ilobilo

module;

#include <version.h>

export module lib:uts;

import std;

// full larp
export namespace lib::uts
{
    constexpr std::string_view sysname { "Linux" };
    constexpr std::string_view release { ILOBILIX_RELEASE };
    constexpr std::string_view machine { ILOBILIX_ARCH };
    // defined in a separate uts.cpp file due to some build system shenanigans
    extern const std::string_view version;
} // export namespace lib::uts
