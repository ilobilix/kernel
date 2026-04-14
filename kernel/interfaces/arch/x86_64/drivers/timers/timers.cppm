// Copyright (C) 2024  ilobilo

export module arch.drivers.timers;

export import x86_64.drivers.timers.hpet;
export import x86_64.drivers.timers.kvm;
export import x86_64.drivers.timers.pit;
export import x86_64.drivers.timers.rtc;
export import x86_64.drivers.timers.tsc;

import system.cpu.self;
import std;

export namespace timers::arch
{
    using namespace x86_64::timers;

    template<auto Func>
    void use_timer(std::size_t ms)
    {
        auto end = Func() + (ms * 1'000'000);
        while (Func() < end) { }
    }

    using calibrator_func = void (*)(std::size_t ms);
    calibrator_func calibrator()
    {
        if (kvm::supported() && cpu::self()->arch.kvm.pvclock)
            return use_timer<kvm::time_ns>;
        else if (hpet::initialised)
            return hpet::calibrate;
        else if (pit::initialised)
            return use_timer<pit::time_ns>;

        return nullptr;
    }

    void init()
    {
        // rtc::init();
        pit::init();
        hpet::init();
        kvm::init();
        tsc::init();
    }
} // export namespace timers::arch