// Copyright (C) 2024-2025  ilobilo

import ilobilix;
import std;

void kthread()
{
    log::debug("entered main kernel thread");

    pci::register_rbs();
    pci::init();

    lib::ensure(vfs::register_fs(fs::tmpfs::init()));
    lib::ensure(vfs::mount(nullptr, "", "/", "tmpfs"));

    initramfs::init();

    pmm::reclaim();

    arch::halt(true);
}

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

        pci::register_ios();
        acpi::init();

        auto thread = sched::thread::create(
            boot::pid0 = sched::process::create(
                nullptr,
                std::make_shared<vmm::pagemap>(vmm::kernel_pagemap.get())
            ), reinterpret_cast<std::uintptr_t>(kthread)
        );
        thread->status = sched::status::ready;
        sched::enqueue(thread, cpu::self()->idx);

        sched::start();
    }
} // extern "C"