// Copyright (C) 2024-2025  ilobilo

module drivers.output;

import drivers.output.framebuffer;
import drivers.output.terminal;
import lib;

namespace output
{
    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "output.available",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task output_task
    {
        "output.init",
        lib::initgraph::presched_init_engine,
        lib::initgraph::entail { lib::initgraph::base_stage(), initialised_stage() },
        [] {
            arch::init();
            frm::init();
            term::init();
        }
    };

    void early_init()
    {
        arch::early_init();
        term::early_init();
    }
} // namespace output