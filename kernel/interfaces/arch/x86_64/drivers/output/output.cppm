// Copyright (C) 2024-2026  ilobilo

export module arch.drivers.output;

export import x86_64.drivers.output.e9;
export import x86_64.drivers.output.uart8250;

export namespace output::arch
{
    void early_init()
    {
        x86_64::output::e9::init();
        x86_64::output::uart8250::init();
    }

    void init() { }
} // export namespace output::arch
