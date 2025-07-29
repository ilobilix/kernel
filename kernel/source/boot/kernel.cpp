// Copyright (C) 2024-2025  ilobilo

import ilobilix;
import cppstd;

extern "C"
{
    [[gnu::used]]
    std::byte kernel_stack[boot::kstack_size] { };

    [[gnu::used]]
    auto kernel_stack_top = kernel_stack + boot::kstack_size;

    [[noreturn]]
    void kmain()
    {
        arch::early_init();
        output::early_init();

        boot::check_requests();

        memory::init();
        cxxabi::construct();

        initgraph::global_init_engine.run();

        sched::start();
    }
} // extern "C"