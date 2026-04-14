// Copyright (C) 2024-2025  ilobilo

import ilobilix;
import std;

extern "C"
{
    [[gnu::used]]
    std::byte kernel_stack[boot::kstack_size] { };

    [[gnu::used]]
    auto kernel_stack_top = kernel_stack + boot::kstack_size;

    [[noreturn]]
    void kthread()
    {
        lib::initgraph::postsched_init_engine.run();
        pmm::reclaim_bootloader_memory();

        sched::thread *thread = nullptr;
        {
            lib::path_view path { "/usr/bin/bash" };
            // lib::info("loading {}", path);

            auto ret = vfs::resolve(std::nullopt, path);
            if (!ret.has_value())
                lib::panic("could not resolve {}", path);

            auto res = vfs::reduce(ret->parent, ret->target);
            if (!res.has_value())
                lib::panic("could not reduce {}", path);

            auto file = vfs::file::create(res.value(), 0, 0, 0);
            auto format = bin::exec::identify(file);
            if (!format)
                lib::panic("could not identify {} file format", path);

            auto pmap = std::make_shared<vmm::pagemap>();
            auto proc = sched::process::create(nullptr, pmap);

            proc->ruid = proc->euid = proc->suid = 1000;
            proc->rgid = proc->egid = proc->sgid = 1000;

            lib::path_view tty_path { "/dev/tty0" };
            ret = vfs::resolve(std::nullopt, tty_path);
            if (!ret.has_value())
                lib::panic("could not resolve {}", tty_path);
            auto tty = vfs::filedesc::create(ret->target, vfs::o_rdwr, proc->pid);
            if (!tty || !tty->file)
                lib::panic("could not create {} filedesc", tty_path);
            if (!tty->file->open(0))
                lib::panic("could not open {}", tty_path);

            proc->fdt.allocate_fd(tty, 0, false);
            proc->fdt.dup(0, 1, false, false);
            proc->fdt.dup(0, 2, false, false);

            thread = format->load({
                .pathname = path.data(),
                .file = file,
                .interp = { },
                .argv = { path.basename().data() },
                .envp = {
                    "TERM=linux",
                    "USER=ilobilix",
                    "HOME=/home/ilobilix",
                    "PATH=/usr/local/bin:/bin:/usr/bin:/sbin:/usr/sbin"
                }
            }, proc);

            if (!thread)
                lib::panic("could not create a thread for {}", path);
        }
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