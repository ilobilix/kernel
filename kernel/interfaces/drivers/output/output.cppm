// Copyright (C) 2024-2025  ilobilo

export module drivers.output;

export import arch.drivers.output;
export import drivers.output.framebuffer;
export import drivers.output.terminal;
export import drivers.output.serial;

import lib;

export namespace output
{
    void early_init();

    lib::initgraph::stage *initialised_stage();
} // export namespace output