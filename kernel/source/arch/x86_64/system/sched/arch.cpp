// Copyright (C) 2024-2026  ilobilo

module system.sched;

import x86_64.system.lapic;
import x86_64.system.gdt;
import x86_64.system.idt;
import system.interrupts;
import system.cpu.arch;

namespace sched::arch
{
    using namespace x86_64;

    namespace
    {
        constexpr std::size_t sched_vector = 0xFD;

        extern "C" void sched_kthread_trampoline();
        extern "C" void sched_uthread_trampoline();

        extern "C" void sched_context_switch(context *prev, context *next, thread_t *self);

        extern "C" [[noreturn]] void sched_kthread_exit()
        {
            thread_exit(0);
        }
    } // namespace

    thread_t *current_thread()
    {
        return reinterpret_cast<thread_t *>(cpu::read_reg<"gs:24">());
    }

    void init_core(thread_t *initial)
    {
        cpu::gs::write(reinterpret_cast<std::uintptr_t>(initial));

        idt::table()[sched_vector].ist = 3;

        auto ret = interrupts::allocate(cpu::self().unsafe_get().idx, sched_vector);
        lib::bug_on(!ret.has_value());

        auto [handler, vec] = *ret;
        lib::bug_on(sched_vector != vec);

        handler.set([](auto) { });
    }

    void init_thread(thread_t *thread, std::uintptr_t ip, std::uintptr_t arg, bool is_kernel)
    {
        auto ctx = std::construct_at(reinterpret_cast<context *>(
            (thread->kstack_top - sizeof(context)) & 0xFul
        ));

        ctx->r12 = ip;
        ctx->r13 = arg;
        ctx->rflags = 0x202;
        ctx->rsp = reinterpret_cast<std::uintptr_t>(ctx);
        ctx->rip = reinterpret_cast<std::uintptr_t>(
            is_kernel ? sched_kthread_trampoline : sched_uthread_trampoline
        );

        thread->ctx = ctx;

        // TODO: user thread trampoline for loading programs

        auto &adata = thread->adata;
        if (!is_kernel)
        {
            const auto &fpu = cpu::features::get_fpu();
            adata.fpu = lib::allocz<std::byte *>(fpu.size);
            adata.fpu_size = fpu.size;
        }
    }

    void context_switch(thread_t *prev, thread_t *next)
    {
        gdt::tss::self().rsp[0] = next->kstack_top;

        if (!prev->is_kernel())
        {
            prev->adata.fs_base = cpu::fs::read();
            prev->adata.gs_base = cpu::gs::read_kernel();
        }

        if (!next->is_kernel())
        {
            cpu::fs::write(next->adata.fs_base);
            cpu::gs::write_kernel(next->adata.gs_base);
        }

        sched_context_switch(prev->ctx, next->ctx, next);
    }

    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack)
    {
        // TODO
        std::unreachable();
    }
} // namespace sched::arch
