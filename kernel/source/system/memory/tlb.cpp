// Copyright (C) 2024-2026  ilobilo

module system.memory.tlb;

import system.memory.phys;
import system.cpu.local;
import system.cpu;
import system.chrono;
import system.sched;
import magic_enum;
import arch;

namespace tlb
{
    constexpr std::size_t max_pages = 32;
    constexpr std::uint64_t timeout_ns = 5'000'000'000ul;

    namespace arch
    {
        using namespace ::arch;
    } // namespace arch

    namespace
    {
        struct alignas(64) record_t
        {
            record_t *next;
            std::atomic<bool> done;
            scope sc;
            std::uintptr_t start;
            std::size_t pages;

            std::uint64_t asid_gen;
            vmm::asid_t asid;
        };

        struct alignas(64) inbox_t
        {
            std::atomic<record_t *> head { nullptr };
        };

        struct cpu_state_t
        {
            struct data_t
            {
                record_t records;
                std::size_t target_idx;
            };
            std::unique_ptr<data_t[]> data;
            lib::bitmap mask;
        };

        cpu_local(inbox_t, inbox);
        cpu_local(cpu_state_t, cpu_state);

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
            if (is_user && cpu::tlb::has_asids() && req.pmap)
            {
                const auto &self = cpu::self().unsafe_get();

                const auto ctx = req.pmap->cached_asid_ctx(self.idx);
                if (!ctx || ctx->gen != self.asid_gen.load(std::memory_order_acquire))
                    return;

                asid = ctx->asid;
            }

            do_flush(req.sc, asid, req.start, req.pages);
        }

        void apply(const record_t &rec, std::uint64_t gen)
        {
            const bool is_user = rec.sc == scope::user_range || rec.sc == scope::user_full;
            if (is_user && rec.asid_gen == 0)
            {
                cpu::tlb::flush_all();
                return;
            }

            vmm::asid_t asid = 0;

            if (is_user)
            {
                if (rec.asid_gen != gen)
                    return;

                if (cpu::tlb::has_asids())
                    asid = rec.asid;
            }

            do_flush(rec.sc, asid, rec.start, rec.pages);
        }

        void publish(std::size_t target_idx, record_t *rec)
        {
            auto &ib = inbox.unsafe_get(cpu::local::nth_base(target_idx));
            auto head = ib.head.load(std::memory_order_relaxed);

            do
            {
                rec->next = head;
            } while (!ib.head.compare_exchange_weak(
                head, rec, std::memory_order_release, std::memory_order_relaxed
            ));
        }
    } // namespace

    void handle_request()
    {
        auto batch = inbox.unsafe_get().head.exchange(nullptr, std::memory_order_acquire);
        if (!batch)
            return;

        const auto &self = cpu::self().unsafe_get();
        const auto gen = self.asid_gen.load(std::memory_order_acquire);

        while (batch)
        {
            const auto next = batch->next;
            apply(*batch, gen);
            batch->done.store(true, std::memory_order_release);
            batch = next;
        }
    }

    void shootdown(const request_t &req)
    {
        sched::preempt_disable();
        flush_local(req);

        if (!cpu::local::available())
        {
            sched::preempt_enable();
            return;
        }

        const auto ncpus = cpu::count();
        if (ncpus <= 1)
        {
            sched::preempt_enable();
            return;
        }

        auto &state = cpu_state.unsafe_get();
        lib::bug_on(!state.data);

        const bool kernel_broadcast = req.sc == scope::kernel_range ||
            req.sc == scope::kernel_full || req.pmap == nullptr || !req.pmap->has_asid_ctx() ||
            !cpu::tlb::has_asids();

        auto &self = cpu::self().unsafe_get();
        const auto self_idx = self.idx;

        std::size_t nt = 0;
        for (std::size_t i = 0; i < ncpus; i++)
        {
            if (i == self_idx)
                continue;

            auto proc = cpu::local::nth(i);
            if (!proc->online.load(std::memory_order_acquire))
                continue;

            auto &rec = state.data[nt].records;
            if (kernel_broadcast)
            {
                rec.asid_gen = 0;
                rec.asid = 0;
                state.data[nt++].target_idx = i;
                continue;
            }

            const auto ctx = req.pmap->cached_asid_ctx(i);
            if (!ctx || ctx->gen != proc->asid_gen.load(std::memory_order_acquire))
                continue;

            rec.asid_gen = ctx->gen;
            rec.asid = ctx->asid;
            state.data[nt++].target_idx = i;
        }

        if (nt == 0)
        {
            sched::preempt_enable();
            return;
        }

        state.mask.clear();
        for (std::size_t i = 0; i < nt; i++)
        {
            auto &rec = state.data[i].records;
            rec.next = nullptr;
            rec.done.store(false, std::memory_order_relaxed);
            rec.sc = req.sc;
            rec.start = req.start;
            rec.pages = req.pages;
            state.mask.set(state.data[i].target_idx, true);
        }

        for (std::size_t i = 0; i < nt; i++)
            publish(state.data[i].target_idx, &state.data[i].records);

        arch::notify_mask(state.mask);

        const auto clock = chrono::main_timer();
        const auto deadline = clock->ns() + timeout_ns;

        const bool status = arch::int_switch_status(true);

        while (true)
        {
            bool all_done = true;
            for (std::size_t i = 0; i < nt; i++)
            {
                if (!state.data[i].records.done.load(std::memory_order_acquire))
                {
                    all_done = false;
                    break;
                }
            }
            if (all_done)
                break;

            arch::pause();

            if (clock->ns() > deadline) [[unlikely]]
            {
                lib::panic("tlb shootdown stuck");
                std::unreachable();
            }
        }

        arch::int_switch(status);
        sched::preempt_enable();
    }

    void init_cpu(std::size_t cpu_idx)
    {
        const auto ncpus = cpu::count();
        auto &state = cpu_state.unsafe_get();
        if (state.data)
            return;

        state.data = std::make_unique<cpu_state_t::data_t[]>(ncpus);
        state.mask.initialise(ncpus);

        arch::install_handler(cpu_idx);
    }
} // namespace tlb
