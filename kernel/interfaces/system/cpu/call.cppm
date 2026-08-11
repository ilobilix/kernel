// Copyright (C) 2024-2026  ilobilo

export module system.cpu.call;

import system.cpu.local;
import system.cpu;
import lib;
import std;

namespace cpu
{
    // implemented by arch
    void install_handler(std::size_t cpu_idx);
    void notify_mask(const lib::bitmap_view mask);
} // namespace cpu

export namespace cpu
{
    struct call_t
    {
        call_t *next;
        std::atomic<bool> done;
        bool (*func)(call_t *self);
    };

    struct wait_policy_t
    {
        std::uint64_t retry_ns = 3'000'000'000ul;
        std::uint64_t timeout_ns = 30'000'000'000ul;
        bool panic = true;
        bool yield = false;
    };

    void handle_ipi();
    void queue(std::size_t target_idx, call_t *call);

    inline void notify(const lib::bitmap_view mask)
    {
        return notify_mask(mask);
    }

    bool wait_for(
        std::size_t num,
        std::function_ref<bool (std::size_t i)> done,
        std::function_ref<std::size_t (std::size_t i)> target,
        lib::bitmap_view buffer, std::string_view name,
        const wait_policy_t &policy = { }
    );

    void init_cpu(std::size_t cpu_idx);

    template<typename Payload>
        requires std::is_trivially_destructible_v<Payload>
    class batch_t
    {
        public:
        struct record_t : call_t
        {
            std::size_t target;
            Payload payload;
        };

        private:
        lib::bitmap _mask;
        std::size_t _num;
        std::unique_ptr<record_t []> _recs;

        public:
        batch_t()
            : _mask { cpu::count() }, _num { 0 },
              _recs { std::make_unique<record_t []>(_mask.size()) } { }

        batch_t(const batch_t &) = delete;
        batch_t &operator=(const batch_t &) = delete;

        bool empty() const { return _num == 0; }
        std::size_t size() const { return _num; }

        record_t &at(std::size_t i) { return _recs[i]; }
        std::size_t target_of(std::size_t i) const { return _recs[i].target; }

        // fill(cpu_idx, payload) returns true if cpu_idx should be included
        // called with preemption disabled
        template<typename Fn>
        void build(Fn &&fill)
        {
            lib::bug_on(cpu::count() > _mask.size());

            _num = 0;
            _mask.clear();

            const auto self_idx = self().unsafe_get().idx;
            for (std::size_t i = 0; i < cpu::count(); i++)
            {
                if (i == self_idx)
                    continue;

                auto proc = local::nth(i);
                if (!proc->online.load(std::memory_order_acquire))
                    continue;

                auto &rec = _recs[_num];
                lib::bug_on(rec.func != nullptr && !rec.done.load(std::memory_order_acquire));

                if (!fill(i, rec.payload))
                    continue;

                rec.next = nullptr;
                rec.done.store(false, std::memory_order_relaxed);
                rec.target = i;

                _mask.set(i, true);
                _num++;
            }
        }

        void dispatch(bool (*func)(call_t *call))
        {
            for (std::size_t i = 0; i < _num; i++)
            {
                _recs[i].func = func;
                queue(_recs[i].target, &_recs[i]);
            }
            notify(_mask);
        }

        bool wait(std::string_view name, const wait_policy_t &policy = { })
        {
            return wait_for(_num,
                [this](std::size_t i) {
                    return _recs[i].done.load(std::memory_order_acquire);
                },
                [this](std::size_t i) { return _recs[i].target; },
                _mask, name, policy
            );
        }

        template<typename Done>
        bool wait_until(Done &&done, std::string_view name, const wait_policy_t &policy = { })
        {
            return wait_for(_num, done,
                [this](std::size_t i) { return _recs[i].target; },
                _mask, name, policy
            );
        }
    };
} // export namespace cpu
