// Copyright (C) 2024-2026  ilobilo

module system.sched;

import x86_64.system.lapic;
import x86_64.system.gdt;
import x86_64.system.idt;
import system.cpu.arch;
import lib;

namespace sched::arch
{
    using namespace x86_64;

    namespace
    {
        extern "C" void sched_kthread_trampoline();
        extern "C" void sched_uthread_trampoline();
        extern "C" void sched_clone_trampoline();

        extern "C" void sched_context_switch(context *prev, context *next, thread_t *self);

        extern "C" [[noreturn]] void sched_return_to_user(std::uintptr_t ip, std::uintptr_t stack);

        extern "C" [[noreturn]] void sched_kthread_exit() { thread_exit(0); }

        extern "C" void sched_trampoline_entry()
        {
            auto thread = current_thread();
            if (auto released = thread->prev_to_release)
            {
                thread->prev_to_release = nullptr;
                released->on_cpu.store(false, std::memory_order_release);
            }
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
                {
                    const sched::siginfo_t info {
                        .signo = sigsegv,
                        .code = si_kernel,
                        .err = 0,
                        .pid = 0,
                        .uid = 0,
                        .status = 0,
                        .addr = thread->set_child_tid,
                        .value = 0
                    };
                    sched::send_signal(thread, info);
                }
                thread->set_child_tid = 0;
            }
        }
    } // namespace

    thread_t *current_thread() { return reinterpret_cast<thread_t *>(cpu::read_reg<"gs:24">()); }

    void init_core(thread_t *initial)
    {
        cpu::gs::write(reinterpret_cast<std::uintptr_t>(initial));

        auto slot = idt::handler_at(cpu::self().unsafe_get().idx, idt::vec_sched);
        lib::bug_on(!slot.has_value() || slot->used());
        slot->set([](cpu::registers *regs) { tick(arch::in_user_mode(regs)); });
    }

    void init_thread(
        thread_t *thread, std::uintptr_t ip, std::uintptr_t arg, bool is_trampoline, bool is_clone
    )
    {
        lib::bug_on(!thread || (thread->is_kernel() && (is_trampoline || is_clone)));

        auto ctx = &thread->ctx;
        ctx->rflags = 0x002;

        if (!thread->is_kernel())
        {
            const auto &fpu = cpu::features::get_fpu();
            auto &adata = thread->adata;
            adata.fpu = lib::allocz<std::byte *>(fpu.size);
            adata.fpu_size = fpu.size;

            if (!is_clone)
            {
                fpu.restore(adata.fpu);

                constexpr std::uint16_t default_fcw = 0b1100111111;
                constexpr std::uint32_t default_mxcsr = 0b1111110000000;

                asm volatile ("fldcw %0" : : "m"(default_fcw) : "memory");
                asm volatile ("ldmxcsr %0" : : "m"(default_mxcsr) : "memory");
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
            apic::ipi(apic::shorthand::self, apic::delivery::fixed, idt::vec_sched);
        else
            apic::arm(ns, idt::vec_sched);
    }

    void wake_up_other(std::size_t cpu_idx)
    {
        apic::ipi(
            cpu::local::nth(cpu_idx)->arch_id, apic::destination::physical, apic::delivery::fixed,
            idt::vec_sched
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

    bool in_user_mode(const cpu::registers *regs) { return (regs->cs & 3) == 3; }

    bool setup_sigframe(
        thread_t *thread, cpu::registers *regs, int sig, const siginfo_t &info,
        const sigaction_t &action
    )
    {
        auto sp = regs->rsp;
        if ((action.flags & sa_onstack) && thread->altstack.sp != 0 && thread->altstack.size > 0)
            sp = thread->altstack.sp + thread->altstack.size;

        sp -= 128;
        auto frame_addr = (sp - sizeof(sigframe_t)) & ~0xFul;
        frame_addr -= 8;

        sigframe_t kf { };
        kf.pretcode = action.restorer;
        kf.info = info;

        const auto mask =
            thread->saved_sigmask.has_value() ? *thread->saved_sigmask : thread->sigmask;

        kf.uc.uc_flags = 0;
        kf.uc.uc_link = nullptr;
        kf.uc.uc_stack = thread->altstack;
        kf.uc.uc_sigmask = mask;

        auto &mc = kf.uc.uc_mcontext;
        mc.r15 = regs->r15;
        mc.r14 = regs->r14;
        mc.r13 = regs->r13;
        mc.r12 = regs->r12;
        mc.r11 = regs->r11;
        mc.r10 = regs->r10;
        mc.r9 = regs->r9;
        mc.r8 = regs->r8;
        mc.rbp = regs->rbp;
        mc.rdi = regs->rdi;
        mc.rsi = regs->rsi;
        mc.rdx = regs->rdx;
        mc.rcx = regs->rcx;
        mc.rbx = regs->rbx;
        mc.rax = regs->rax;
        mc.rip = regs->rip;
        mc.cs = regs->cs;
        mc.rflags = regs->rflags;
        mc.rsp = regs->rsp;
        mc.ss = regs->ss;

        if (!lib::copy_to_user(reinterpret_cast<void __user *>(frame_addr), &kf, sizeof(kf)))
            return false;

        regs->rip = action.handler;
        regs->rsp = frame_addr;
        regs->rdi = sig;
        regs->rsi = frame_addr + __builtin_offsetof(sigframe_t, info);
        regs->rdx = frame_addr + __builtin_offsetof(sigframe_t, uc);
        regs->rax = 0;
        regs->rcx = 0;
        regs->r11 = 0;

        auto new_mask = mask | action.mask;
        if (!(action.flags & sa_nodefer))
            new_mask.add(sig);
        new_mask &= ~sigmask_uncatchable;
        thread->sigmask = new_mask;
        thread->saved_sigmask = std::nullopt;

        return true;
    }

    bool restore_sigframe(thread_t *thread, cpu::registers *regs)
    {
        const auto uc_addr = regs->rsp;
        ucontext_t uc;
        if (!lib::copy_from_user(
                &uc, reinterpret_cast<const ucontext_t __user *>(uc_addr), sizeof(uc)
            ))
            return false;

        const auto &mc = uc.uc_mcontext;

        regs->r15 = mc.r15;
        regs->r14 = mc.r14;
        regs->r13 = mc.r13;
        regs->r12 = mc.r12;
        regs->r11 = mc.r11;
        regs->r10 = mc.r10;
        regs->r9 = mc.r9;
        regs->r8 = mc.r8;
        regs->rbp = mc.rbp;
        regs->rdi = mc.rdi;
        regs->rsi = mc.rsi;
        regs->rdx = mc.rdx;
        regs->rcx = mc.rcx;
        regs->rbx = mc.rbx;
        regs->rax = mc.rax;
        regs->rip = mc.rip;
        regs->rsp = mc.rsp;

        regs->cs = static_cast<std::uint64_t>(gdt::segment::ucode) | 0x03;
        regs->ss = static_cast<std::uint64_t>(gdt::segment::udata) | 0x03;

        regs->rflags = (mc.rflags & 0xDD5) | 0x202;

        thread->sigmask = uc.uc_sigmask & ~sigmask_uncatchable;
        thread->altstack = uc.uc_stack;

        return true;
    }
} // namespace sched::arch
