// Copyright (C) 2024-2026  ilobilo

import ilobilix;
import std;

std::byte kernel_stack[boot::kstack_size] { };

extern "C" [[gnu::used]]
auto kernel_stack_top = kernel_stack + boot::kstack_size;

void kthread()
{
    lib::initgraph::postsched_init_engine.run();
    pmm::reclaim_bootloader_memory();

    sched::thread_t *thread = nullptr;
    {
        lib::path_view path { "/usr/bin/bash" };
        lib::info("loading {}", path);

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

        auto proc = sched::create_process(nullptr);

        proc->vmspace = std::make_shared<vmm::vmspace>(
            std::make_shared<vmm::pagemap>()
        );

        proc->vfs = std::make_shared<sched::process_t::vfs_state>();
        proc->vfs->root = vfs::get_root(true);
        proc->vfs->cwd = proc->vfs->root;

        proc->fdt = std::make_shared<vfs::fdtable>();
        proc->cred = std::make_shared<sched::cred_t>();

        proc->cred->ruid = proc->cred->euid = proc->cred->suid = 1000;
        proc->cred->rgid = proc->cred->egid = proc->cred->sgid = 1000;

        lib::path_view tty_path { "/dev/ttyS0" };
        ret = vfs::resolve(std::nullopt, tty_path);
        if (!ret.has_value())
            lib::panic("could not resolve {}", tty_path);
        auto tty = vfs::filedesc::create(ret->target, vfs::o_rdwr, proc->pid);
        if (!tty || !tty->file)
            lib::panic("could not create {} filedesc", tty_path);
        if (!tty->file->open(0))
            lib::panic("could not open {}", tty_path);

        proc->fdt->alloc(tty, 0, false);
        proc->fdt->dup(0, 1, false, false);
        proc->fdt->dup(0, 2, false, false);

        thread = format->load({
            .pathname = path.data(),
            .file = file,
            .interp = { },
            .argv = { path.basename().data() },
            .envp = {
                "TERM=xterm-256color",
                "USER=ilobilix",
                "HOME=/home/ilobilix",
                "PATH=/usr/local/bin:/bin:/usr/bin:/sbin:/usr/sbin"
            }
        }, proc);

        if (!thread)
            lib::panic("could not create a thread for {}", path);
    }

    lib::log::wait_for_logs();
    sched::enqueue_new(thread);
}

extern "C"  [[noreturn]] void kmain()
{
    arch::early_init();
    output::early_init();

    boot::check_requests();

    memory::init();
    cxxabi::construct();

    lib::initgraph::presched_init_engine.run();

    sched::spawn(kthread);
    sched::start();
}
