// Copyright (C) 2024-2025  ilobilo

module drivers.output;

import drivers.output.framebuffer;
import drivers.output.terminal;
import lib;

namespace output
{
    initgraph::stage *available_stage()
    {
        static initgraph::stage stage { "full-output" };
        return &stage;
    }

    initgraph::task output_task
    {
        "init-output",
        initgraph::entail { available_stage() },
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