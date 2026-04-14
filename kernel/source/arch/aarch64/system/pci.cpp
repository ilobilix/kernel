// Copyright (C) 2024-2025  ilobilo

module system.pci;

import lib;
import std;

namespace pci
{
    namespace arch
    {
        lib::initgraph::stage *ios_discovered_stage();
        lib::initgraph::stage *rbs_discovered_stage();
    } // namespace arch
} // namespace pci