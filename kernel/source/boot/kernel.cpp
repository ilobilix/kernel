// Copyright (C) 2024-2025  ilobilo

import ilobilix;
import cppstd;

extern "C"
{
    [[gnu::used]]
    std::byte kernel_stack[boot::kstack_size] { };

    [[gnu::used]]
    auto kernel_stack_top = kernel_stack + boot::kstack_size;

    void kthread()
    {
        lib::initgraph::postsched_init_engine.run();
        pmm::reclaim_bootloader_memory();

        lib::path_view path { "/usr/bin/bash" };
        log::info("loading {}", path);

        auto ret = vfs::resolve(std::nullopt, path);
        if (!ret.has_value())
            lib::panic("could not resolve {}", path);

        auto res = vfs::reduce(ret->parent, ret->target);
        if (!res.has_value())
            lib::panic("could not reduce {}", path);

        auto file = vfs::file::create(res.value(), 0, 0);
        auto format = bin::exec::identify(file);
        if (!format)
            lib::panic("could not identify {} file format", path);

        auto pmap = std::make_shared<vmm::pagemap>();
        auto proc = sched::process::create(nullptr, pmap);

        auto cons = vfs::create(std::nullopt, "/dev/tty", 0666 | stat::s_ifchr, dev::makedev(5, 0));
        lib::panic_if(!cons, "could not create /dev/tty");
        proc->fdt.allocate_fd(vfs::filedesc::create(cons.value(), vfs::o_rdwr), 0, false);
        proc->fdt.allocate_fd(vfs::filedesc::create(cons.value(), vfs::o_rdwr), 0, false);
        proc->fdt.allocate_fd(vfs::filedesc::create(cons.value(), vfs::o_rdwr), 0, false);

        auto thread = format->load({
            .pathname = "/usr/bin/bash",
            .file = file,
            .interp = { },
            .argv = { "bash" },
            .envp = {
                "TERM=linux",
                "USER=ilobilix",
                "HOME=/home/ilobilix",
                "PATH=/usr/local/bin:/bin:/usr/bin:/sbin:/usr/sbin"
            }
        }, proc);

        if (!thread)
            lib::panic("could not create a thread for {}", path);

        thread->status = sched::status::ready;
        sched::enqueue(thread, sched::allocate_cpu());

        arch::halt();
    }

    [[noreturn]]
    void kmain()
    {
        arch::early_init();
        output::early_init();

        boot::check_requests();

        memory::init();
        cxxabi::construct();

        lib::initgraph::presched_init_engine.run();

        sched::spawn(0, reinterpret_cast<std::uintptr_t>(kthread));
        sched::start();
    }
} // extern "C"