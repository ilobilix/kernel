// Copyright (C) 2024-2025  ilobilo

export module arch.drivers.output;

export import x86_64.drivers.output.com;
export import x86_64.drivers.output.e9;

export namespace output::arch
{
    void early_init()
    {
        x86_64::output::com::init();
        x86_64::output::e9::init();
    }

    void init() { }
} // export namespace output::arch