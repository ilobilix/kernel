// Copyright (C) 2024  ilobilo

export module arch.drivers.timers;
import std;

export namespace timers::arch
{
    using calibrator_func = void (*)(std::size_t ms);
    calibrator_func calibrator() { return nullptr; }
    void init()
    {
    }
} // export namespace timers::arch