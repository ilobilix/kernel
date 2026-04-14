// Copyright (C) 2024-2025  ilobilo

export module arch.drivers.output;

export import aarch64.drivers.output.pl011;

export namespace output::arch
{
    void early_init() { }

    void init()
    {
        aarch64::output::pl011::init();
    }
} // export namespace output::arch