// Copyright (C) 2024-2025  ilobilo

module system.pci;

import lib;
import cppstd;

namespace pci
{
    namespace arch
    {
        initgraph::stage *ios_discovered_stage();
        initgraph::stage *rbs_discovered_stage();
    } // namespace arch
} // namespace pci