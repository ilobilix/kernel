// Copyright (C) 2024  ilobilo

export module arch.drivers.timers;

export import x86_64.drivers.timers.hpet;
export import x86_64.drivers.timers.kvm;
export import x86_64.drivers.timers.pit;
export import x86_64.drivers.timers.rtc;
export import x86_64.drivers.timers.tsc;

export namespace timers::arch
{
    void init()
    {
        using namespace x86_64::timers;

        // rtc::init();
        pit::init();
        // hpet::init();
        kvm::init();
        tsc::init();
    }
} // export namespace timers::arch