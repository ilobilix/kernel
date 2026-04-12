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
        extern "C" void sched_clone_trampoline();

        extern "C" void sched_context_switch(context *prev, context *next, thread_t *self);

        extern "C" [[noreturn]] void sched_return_to_user(std::uintptr_t ip, std::uintptr_t stack);

        extern "C" [[noreturn]] void sched_kthread_exit()
        {
            thread_exit(0);
        }

        extern "C" void sched_trampoline_entry()
        {
            auto thread = current_thread();
            if (thread->needs_unlock)
            {
                thread->needs_unlock->unlock();
                thread->needs_unlock = nullptr;
            }

            if (thread->was_in_interrupt)
            {
                thread->was_in_interrupt->store(false, std::memory_order_release);
                thread->was_in_interrupt = nullptr;
            }

            if (thread->set_child_tid)
            {
                auto set_child_tid = reinterpret_cast<pid_t __user *>(thread->set_child_tid);
                if (!lib::copy_to_user(set_child_tid, &thread->tid, sizeof(pid_t)))
                    // TODO: kill thread instead
                    lib::panic("failed to write to set_child_tid");
                thread->set_child_tid = 0;
            }
        }
    } // namespace

    thread_t *current_thread()
    {
        return reinterpret_cast<thread_t *>(cpu::read_reg<"gs:24">());
    }

    void init_core(thread_t *initial)
    {
        cpu::gs::write(reinterpret_cast<std::uintptr_t>(initial));

        auto ret = interrupts::allocate(cpu::self().unsafe_get().idx, sched_vector);
        lib::bug_on(!ret.has_value());

        auto [handler, vec] = *ret;
        lib::bug_on(sched_vector != vec);

        handler.set([](auto) { tick(); });
    }

    void init_thread(
        thread_t *thread, std::uintptr_t ip, std::uintptr_t arg,
        bool is_trampoline, bool is_clone
    )
    {
        lib::bug_on(!thread || (thread->is_kernel() && (is_trampoline || is_clone)));

        auto ctx = &thread->ctx;
        ctx->rflags = 0x002;

        if (!thread->is_kernel())
        {
            // TODO-SCHED-REWRITE: lazy fpu
            const auto &fpu = cpu::features::get_fpu();
            auto &adata = thread->adata;
            adata.fpu = lib::allocz<std::byte *>(fpu.size);
            adata.fpu_size = fpu.size;

            if (!is_clone)
            {
                fpu.restore(adata.fpu);

                constexpr std::uint16_t default_fcw = 0b1100111111;
                constexpr std::uint32_t default_mxcsr = 0b1111110000000;

                asm volatile ("fldcw %0" :: "m"(default_fcw) : "memory");
                asm volatile ("ldmxcsr %0" :: "m"(default_mxcsr) : "memory");
            }
            fpu.save(adata.fpu);
        }

        if (is_clone)
        {
            ctx->rip = reinterpret_cast<std::uintptr_t>(sched_clone_trampoline);
            return;
        }

        constexpr std::uintptr_t trampoline_rsp_reserve = 256;
        ctx->rsp = thread->kstack_top - trampoline_rsp_reserve;

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
        }
    }

    void deinit_thread(thread_t *thread)
    {
        if (thread->adata.fpu)
            lib::free(thread->adata.fpu);
    }

    void arm_timer_ns(std::uint64_t ns)
    {
        if (ns == 0)
            apic::ipi(apic::shorthand::self, apic::delivery::fixed, sched_vector);
        else
            apic::arm(ns, sched_vector);
    }

    void wake_up_other(std::size_t cpu_idx)
    {
        apic::ipi(
            cpu::local::nth(cpu_idx)->arch_id,
            apic::destination::physical,
            apic::delivery::fixed,
            sched_vector
        );
    }

    void context_switch(thread_t *prev, thread_t *next)
    {
        const auto &fpu = cpu::features::get_fpu();

        if (!prev->is_kernel())
        {
            prev->adata.fs_base = cpu::fs::read();
            prev->adata.gs_base = cpu::gs::read_kernel();
            if (prev->adata.fpu)
                fpu.save(prev->adata.fpu);
        }

        if (!next->is_kernel())
        {
            gdt::tss::self().rsp[0] = next->kstack_top;

            cpu::fs::write(next->adata.fs_base);
            cpu::gs::write_kernel(next->adata.gs_base);
            if (next->adata.fpu)
                fpu.restore(next->adata.fpu);
        }

        sched_context_switch(&prev->ctx, &next->ctx, next);
    }

    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack)
    {
        sched_return_to_user(ip, stack);
    }
} // namespace sched::arch
