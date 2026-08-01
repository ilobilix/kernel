// Copyright (C) 2024-2026  ilobilo

module system.cpu.call;

import system.chrono;
import system.sched;
import arch;

namespace cpu
{
    namespace
    {
        struct alignas(64) inbox_t
        {
            std::atomic<call_t *> head { nullptr };
        };
        cpu_local(inbox_t, inbox);
    } // namespace

    void handle_ipi()
    {
        auto batch = inbox.unsafe_get().head.exchange(nullptr, std::memory_order_acquire);
        while (batch)
        {
            const auto next = batch->next;
            if (batch->func(batch))
                batch->done.store(true, std::memory_order_release);
            batch = next;
        }
    }

    void queue(std::size_t target_idx, call_t *call)
    {
        auto &ib = inbox.unsafe_get(local::nth_base(target_idx));
        auto head = ib.head.load(std::memory_order_relaxed);

        do {
            call->next = head;
        } while (!ib.head.compare_exchange_weak(head, call,
            std::memory_order_release, std::memory_order_relaxed));
    }

    bool wait_for(
        std::size_t num,
        std::function_ref<bool (std::size_t i)> done,
        std::function_ref<std::size_t (std::size_t i)> target,
        lib::bitmap &buffer, std::string_view name,
        const wait_policy_t &policy
    )
    {
        if (num == 0)
            return true;

        const auto clock = chrono::main_timer();
        const auto start = clock->ns();
        auto next_retry = start + policy.retry_ns;

        while (true)
        {
            bool all_done = true;
            for (std::size_t i = 0; i < num; i++)
            {
                if (!done(i))
                {
                    all_done = false;
                    break;
                }
            }
            if (all_done)
                return true;

            if (policy.yield)
                sched::yield();
            else
                arch::pause();

            const auto now = clock->ns();
            if (now <= next_retry) [[likely]]
                continue;

            std::uint64_t pending = 0;
            buffer.clear();
            for (std::size_t i = 0; i < num; i++)
            {
                if (done(i))
                    continue;

                const auto idx = target(i);
                if (idx < 64)
                    pending |= 1ul << idx;
                buffer.set(idx, true);
            }

            if (buffer.empty())
                continue;

            if (now - start > policy.timeout_ns) [[unlikely]]
            {
                if (policy.panic)
                {
                    lib::panic("cpu: '{}' stuck! pending: 0x{:X}", name, pending);
                    std::unreachable();
                }

                lib::error(
                    "cpu: '{}' timed out after {} ms! pending: 0x{:X}",
                    name, (now - start) / 1'000'000, pending
                );
                return false;
            }

            lib::warn(
                "cpu: '{}' slow after {} ms! pending: 0x{:X}, trying again",
                name, (now - start) / 1'000'000, pending
            );
            notify_mask(buffer);
            next_retry = now + policy.retry_ns;
        }
    }

    void init_cpu(std::size_t cpu_idx)
    {
        install_handler(cpu_idx);
    }
} // namespace cpu
