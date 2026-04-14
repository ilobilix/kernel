// Copyright (C) 2024-2025  ilobilo

import ilobilix;
import std;

extern "C"
{
    std::byte kernel_stack[boot::kernel_stack_size] { };
    auto kernel_stack_top = kernel_stack + boot::kernel_stack_size;

    void kmain()
    {
        serial::early_init();
        boot::check_requests();

        memory::init();
        serial::init();
        cxxabi::construct();

        frm::init();
        term::init();

        acpi::early();
        arch::init();
        pmm::reclaim();

        pci::register_ios();
        acpi::init();
        pci::register_rbs();
        pci::init();

        lib::panic("get stick bugged lol");

        arch::halt();
    }
} // extern "C"