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

        extern "C" [[noreturn]] void sched_return_to_user(std::uintptr_t ip, std::uintptr_t stack);

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

        handler.set([](auto) { tick(); });
    }

    void init_thread(thread_t *thread, std::uintptr_t ip, std::uintptr_t arg, bool is_trampoline)
    {
        auto ctx = std::construct_at(reinterpret_cast<context *>(
            (thread->kstack_top - sizeof(context)) & ~0xFul
        ));

        ctx->rflags = 0x202;
        ctx->rsp = reinterpret_cast<std::uintptr_t>(ctx);

        auto &adata = thread->adata;
        if (thread->is_kernel())
        {
            ctx->r12 = ip;
            ctx->r13 = arg;
            ctx->rip = reinterpret_cast<std::uintptr_t>(sched_kthread_trampoline);
        }
        else
        {
            if (is_trampoline)
            {
                ctx->r12 = ip;
                ctx->r13 = arg;
            }
            else
            {
                ctx->r12 = 0;
                ctx->r13 = ip;
                ctx->r15 = thread->ustack_top;
            }

            ctx->rip = reinterpret_cast<std::uintptr_t>(sched_uthread_trampoline);

            const auto &fpu = cpu::features::get_fpu();
            adata.fpu = lib::allocz<std::byte *>(fpu.size);
            adata.fpu_size = fpu.size;

            // TODO-SCHED-REWRITE: lazy fpu
        }

        thread->ctx = ctx;
    }

    void arm_timer_ns(std::uint64_t ns)
    {
        if (ns == 0)
            apic::ipi(apic::shorthand::self, apic::delivery::fixed, sched_vector);
        else
            apic::arm(ns, sched_vector);
    }

    void context_switch(thread_t *prev, thread_t *next)
    {
        if (!prev->is_kernel())
        {
            prev->adata.fs_base = cpu::fs::read();
            prev->adata.gs_base = cpu::gs::read_kernel();
        }

        if (!next->is_kernel())
        {
            gdt::tss::self().rsp[0] = next->kstack_top;

            cpu::fs::write(next->adata.fs_base);
            cpu::gs::write_kernel(next->adata.gs_base);
        }

        sched_context_switch(prev->ctx, next->ctx, next);
    }

    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack)
    {
        sched_return_to_user(ip, stack);
    }
} // namespace sched::arch
