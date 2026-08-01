// Copyright (C) 2024-2026  ilobilo

module system.memory.tlb;

import system.memory.phys;
import system.cpu.local;
import system.cpu.call;
import system.cpu;
import system.sched;
import arch;

namespace tlb
{
    constexpr std::size_t max_pages = 32;

    namespace
    {
        struct payload_t
        {
            scope sc;
            std::uintptr_t start;
            std::size_t pages;

            std::uint64_t asid_gen;
            vmm::asid_t asid;
        };

        cpu_local(cpu::batch_t<payload_t>, batch);

        void do_flush(scope sc, vmm::asid_t asid, std::uintptr_t start, std::size_t pages)
        {
            const auto threshold = max_pages * (cpu::tlb::has_asids() ? 1 : 4);
            if (sc == scope::user_full || sc == scope::kernel_full || pages > threshold)
            {
                if (cpu::tlb::has_asids())
                    cpu::tlb::flush_asid(asid);
                else
                    cpu::tlb::flush_all();
                return;
            }

            if (cpu::tlb::has_asids())
            {
                for (std::size_t i = 0; i < pages; i++)
                    cpu::tlb::flush_page(start + i * pmm::page_size, asid);
            }
            else
            {
                for (std::size_t i = 0; i < pages; i++)
                    cpu::tlb::flush_page(start + i * pmm::page_size);
            }
        }

        void flush_local(const request_t &req)
        {
            const bool is_user = req.sc == scope::user_range || req.sc == scope::user_full;

            vmm::asid_t asid = 0;
            if (is_user && req.pmap)
            {
                const auto &self = cpu::self().unsafe_get();

                const auto ctx = req.pmap->cached_asid_ctx(self.idx);
                if (!ctx || ctx->gen != self.asid_gen.load(std::memory_order_acquire))
                    return;

                if (cpu::tlb::has_asids())
                    asid = ctx->asid;
            }

            do_flush(req.sc, asid, req.start, req.pages);
        }

        void apply(const payload_t &pl, std::uint64_t gen)
        {
            const bool is_user = pl.sc == scope::user_range || pl.sc == scope::user_full;
            if (is_user && pl.asid_gen == 0)
            {
                cpu::tlb::flush_all();
                return;
            }

            vmm::asid_t asid = 0;

            if (is_user)
            {
                if (pl.asid_gen != gen)
                    return;

                if (cpu::tlb::has_asids())
                    asid = pl.asid;
            }

            do_flush(pl.sc, asid, pl.start, pl.pages);
        }
    } // namespace

    void local_flush(const request_t &req)
    {
        sched::preempt_disable();
        flush_local(req);
        sched::preempt_enable();
    }

    void shootdown(const request_t &req)
    {
        sched::preempt_disable();
        flush_local(req);

        if (!cpu::local::available() || cpu::count() <= 1)
        {
            sched::preempt_enable();
            return;
        }

        const bool kernel_broadcast =
            req.sc == scope::kernel_range || req.sc == scope::kernel_full ||
            req.pmap == nullptr || !req.pmap->has_asid_ctx();

        auto &bt = batch.unsafe_get();

        std::atomic_thread_fence(std::memory_order_seq_cst);

        bt.build([&](std::size_t i, payload_t &pl) {
            pl.sc = req.sc;
            pl.start = req.start;
            pl.pages = req.pages;

            if (kernel_broadcast)
            {
                pl.asid_gen = 0;
                pl.asid = 0;
                return true;
            }

            const auto ctx = req.pmap->cached_asid_ctx(i);
            if (!ctx || ctx->gen != cpu::local::nth(i)->asid_gen.load(std::memory_order_acquire))
                return false;

            pl.asid_gen = ctx->gen;
            pl.asid = ctx->asid;
            return true;
        });

        if (bt.empty())
        {
            sched::preempt_enable();
            return;
        }

        bt.dispatch([](cpu::call_t *call) {
            const auto rec = static_cast<cpu::batch_t<payload_t>::record_t *>(call);
            const auto gen = cpu::self().unsafe_get().asid_gen.load(std::memory_order_acquire);
            apply(rec->payload, gen);
            return true;
        });

        const bool status = arch::int_switch_status(true);
        bt.wait("tlb shootdown");
        arch::int_switch(status);

        sched::preempt_enable();
    }
} // namespace tlb
