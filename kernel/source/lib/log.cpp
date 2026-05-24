// Copyright (C) 2024-2026  ilobilo

module;

#include <flanterm.h>

module lib;

import drivers.output.terminal;
import drivers.output.serial;
import system.sched;
import system.chrono;
import arch;
import std;

namespace lib::log
{
    namespace
    {
        sched::wait_queue_t available;
        sched::wait_queue_t finished;

        constinit std::atomic_uint64_t last_published = 0;
        constinit std::atomic_uint64_t last_consumed = 0;
        constinit std::uint64_t next_seq = 0;

        constinit std::atomic_uint64_t dropped = 0;

        constinit lib::spinlock_irq lock;
        constinit std::atomic_bool direct = true;

        constinit logger *loggers = nullptr;

        // based on printk_ringbuffer.c from linux and https://github.com/rdmsr/zag/blob/de98aab7f068221872f2b110d371c1037e2cfe15/src/kern/log_ring.zig
        template<std::size_t DataBits, std::size_t DescBits>
        class ring
        {
            static_assert(DataBits > DescBits);
            static_assert(DescBits >= 1);

            public:
            using id_t = unsigned _BitInt(62);

            enum class error
            {
                too_large,
                ring_full,
                invalid_seq,
                data_lost,
                not_yet_available
            };

            struct info
            {
                std::uint64_t seq;
                std::uint64_t time;
                std::uint16_t len;
                level lvl;
            };

            struct reservation
            {
                bool prev_irqs;
                id_t id;
                info *meta;
                std::span<char> buf;
            };

            static constexpr std::size_t data_size = 1ul << DataBits;
            static constexpr std::size_t desc_count = 1ul << DescBits;
            static constexpr std::size_t max_size = data_size / 2;

            private:
            static constexpr std::size_t data_mask = data_size - 1;
            static constexpr std::size_t desc_mask = desc_count - 1;

            static constexpr std::size_t no_data = 1;

            enum : std::uint32_t
            {
                st_reserved = 0,
                st_finalised = 1,
                st_reusable = 2,
                st_miss = 3
            };

            struct desc_state
            {
                id_t id;
                std::uint32_t state;

                constexpr std::uint64_t pack() const
                {
                    return static_cast<std::uint64_t>(id) |
                          (static_cast<std::uint64_t>(state) << 62);
                }

                static constexpr desc_state unpack(std::uint64_t raw)
                {
                    return {
                        .id = static_cast<id_t>(raw & ((1ul << 62) - 1)),
                        .state = static_cast<std::uint32_t>(raw >> 62)
                    };
                }
            };

            struct desc
            {
                mutable std::uint64_t state;
                std::size_t data_begin;
                std::size_t data_end;
            };

            struct alignas(std::uint64_t) data_block
            {
                std::uint64_t owner;
                char text[];
            };

            static std::atomic_ref<std::uint64_t> state_ref(const desc &dsc)
            {
                return std::atomic_ref<std::uint64_t> { dsc.state };
            }

            static std::atomic_ref<std::uint64_t> owner_ref(data_block &blk)
            {
                return std::atomic_ref<std::uint64_t> { blk.owner };
            }

            std::array<desc, desc_count> descs;
            std::array<info, desc_count> infos;
            std::array<std::byte, data_size> data;

            std::atomic_size_t head_id;
            std::atomic_size_t tail_id;
            std::atomic_size_t head_lpos;
            std::atomic_size_t tail_lpos;

            desc *desc_at(id_t id) { return &descs[id & desc_mask]; }
            const desc *desc_at(id_t id) const { return &descs[id & desc_mask]; }
            info *info_at(id_t id) { return &infos[id & desc_mask]; }
            const info *info_at(id_t id) const { return &infos[id & desc_mask]; }

            data_block *block_at(std::size_t lpos)
            {
                return std::launder(reinterpret_cast<data_block *>(&data[lpos & data_mask]));
            }

            static constexpr id_t prev_wrap(id_t id)
            {
                return id - static_cast<id_t>(desc_count);
            }

            static constexpr id_t sentinel_id()
            {
                return static_cast<id_t>(0) - static_cast<id_t>(desc_count + 1);
            }

            static constexpr std::size_t block_size(std::size_t payload)
            {
                constexpr auto align = sizeof(std::size_t);
                return (payload + sizeof(data_block) + align - 1) & ~(align - 1);
            }

            static constexpr bool wraps(std::size_t begin, std::size_t end)
            {
                return (begin >> DataBits) != ((end - 1) >> DataBits);
            }

            static constexpr std::size_t next_lpos(std::size_t lpos, std::size_t size)
            {
                const auto end = lpos + size;
                if (wraps(lpos, end))
                    return ((end >> DataBits) << DataBits) + size;
                return end;
            }

            static constexpr bool needs_room(std::size_t cur, std::size_t target)
            {
                return target - cur - 1 < data_size;
            }

            static std::uint32_t classify(id_t expected, desc_state s)
            {
                return expected != s.id ? st_miss : s.state;
            }

            struct snapshot_t
            {
                std::uint32_t state;
                std::uint64_t seq;
                desc_state ds;
                std::size_t data_begin;
                std::size_t data_end;
            };

            snapshot_t snapshot(id_t id) const
            {
                const auto desc = desc_at(id);
                const auto info = info_at(id);

                auto ds = desc_state::unpack(state_ref(*desc).load(std::memory_order_acquire));
                snapshot_t out {
                    .state = classify(id, ds),
                    .seq = 0,
                    .ds = ds,
                    .data_begin = 0,
                    .data_end = 0
                };

                if (out.state == st_miss || out.state == st_reserved)
                    return out;

                out.data_begin = desc->data_begin;
                out.data_end = desc->data_end;
                out.seq = info->seq;

                rmb();

                ds = desc_state::unpack(state_ref(*desc).load(std::memory_order_acquire));
                out.state = classify(id, ds);
                out.ds = ds;
                return out;
            }
            void mark_reusable(id_t id)
            {
                auto from = desc_state { .id = id, .state = st_finalised } .pack();
                const auto to = desc_state { .id = id, .state = st_reusable } .pack();
                state_ref(*desc_at(id)).compare_exchange_strong(
                    from, to,
                    std::memory_order_release,
                    std::memory_order_relaxed
                );
            }

            std::optional<std::size_t> free_data(std::size_t begin, std::size_t target)
            {
                auto cur = begin;
                while (needs_room(cur, target))
                {
                    auto blk = block_at(cur);
                    const auto owner = static_cast<id_t>(owner_ref(*blk).load(std::memory_order_relaxed));

                    const auto snap = snapshot(owner);
                    switch (snap.state)
                    {
                        case st_miss:
                        case st_reserved:
                            return std::nullopt;
                        case st_finalised:
                            if (snap.data_begin != cur)
                                return std::nullopt;
                            mark_reusable(owner);
                            break;
                        case st_reusable:
                            if (snap.data_begin != cur)
                                return std::nullopt;
                            break;
                    }
                    cur = snap.data_end;
                }
                return cur;
            }

            std::expected<void, error> advance_data_tail(std::size_t target)
            {
                if (target & 1)
                    return { };

                auto tail = tail_lpos.load(std::memory_order_relaxed);
                while (needs_room(tail, target))
                {
                    if (const auto next = free_data(tail, target))
                    {
                        if (tail_lpos.compare_exchange_weak(tail, *next,
                                std::memory_order_release,
                                std::memory_order_relaxed
                            ))
                            break;
                    }
                    else
                    {
                        const auto cur = tail_lpos.load(std::memory_order_acquire);
                        if (cur == tail)
                            return std::unexpected { error::ring_full };
                        tail = cur;
                    }
                    arch::pause();
                }
                return { };
            }

            std::expected<void, error> advance_desc_tail(id_t tid)
            {
                const auto snap = snapshot(tid);
                switch (snap.state)
                {
                    case st_finalised:
                        mark_reusable(tid);
                        break;
                    case st_reserved:
                        return std::unexpected { error::ring_full };
                    case st_miss:
                        if (snap.ds.id == prev_wrap(tid))
                            return std::unexpected { error::ring_full };
                        return { };
                    case st_reusable:
                        break;
                }

                if (const auto res = advance_data_tail(snap.data_end); !res)
                    return res;

                const auto next = snapshot(static_cast<id_t>(tid + 1));
                if (next.state == st_finalised || next.state == st_reusable)
                {
                    auto cur = static_cast<std::size_t>(tid);
                    tail_id.compare_exchange_strong(cur,
                        static_cast<std::size_t>(tid + 1),
                        std::memory_order_release,
                        std::memory_order_relaxed
                    );
                    return { };
                }

                const auto cur = tail_id.load(std::memory_order_acquire);
                if (cur != static_cast<std::size_t>(tid))
                    return { };
                return std::unexpected { error::ring_full };
            }

            std::expected<id_t, error> reserve_desc()
            {
                auto head = static_cast<id_t>(head_id.load(std::memory_order_acquire));
                id_t new_id;

                while (true)
                {
                    new_id = head + 1;
                    const auto prev = prev_wrap(new_id);

                    const auto cur_tail = static_cast<id_t>(tail_id.load(std::memory_order_acquire));
                    if (prev == cur_tail)
                    {
                        if (const auto res = advance_desc_tail(prev); !res)
                            return std::unexpected { res.error() };
                    }

                    auto raw = static_cast<std::size_t>(head);
                    if (head_id.compare_exchange_weak(raw, static_cast<std::size_t>(new_id),
                            std::memory_order_acq_rel,
                            std::memory_order_acquire
                        ))
                        break;

                    head = static_cast<id_t>(raw);
                    arch::pause();
                }

                auto desc = desc_at(new_id);
                auto from_raw = state_ref(*desc).load(std::memory_order_relaxed);
                const auto from = desc_state::unpack(from_raw);

                if (from_raw != 0 && classify(prev_wrap(new_id), from) != st_reusable)
                    return std::unexpected { error::ring_full };

                const auto to_raw = desc_state { .id = new_id, .state = st_reserved } .pack();
                if (!state_ref(*desc).compare_exchange_strong(from_raw, to_raw,
                        std::memory_order_release,
                        std::memory_order_relaxed
                    ))
                    return std::unexpected { error::ring_full };

                return new_id;
            }

            std::expected<std::span<char>, error> alloc_data(
                std::size_t size, id_t owner,
                std::size_t &out_begin, std::size_t &out_end
            )
            {
                const auto bsize = block_size(size);
                auto head = head_lpos.load(std::memory_order_relaxed);

                while (true)
                {
                    const auto new_head = next_lpos(head, bsize);
                    if (const auto res = advance_data_tail(new_head - data_size); !res)
                    {
                        out_begin = out_end = no_data;
                        return std::unexpected { res.error() };
                    }

                    if (head_lpos.compare_exchange_weak(head, new_head,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire
                        ))
                    {
                        auto blk = block_at(head);
                        owner_ref(*blk).store(
                            static_cast<std::uint64_t>(owner),
                            std::memory_order_relaxed
                        );

                        if (wraps(head, new_head))
                        {
                            blk = block_at(0);
                            owner_ref(*blk).store(
                                static_cast<std::uint64_t>(owner),
                                std::memory_order_relaxed
                            );
                        }

                        out_begin = head;
                        out_end = new_head;
                        return std::span<char>(blk->text, size);
                    }
                    arch::pause();
                }
            }

            std::expected<info, error> read_one(std::uint64_t seq, std::span<char> buf)
            {
                const auto id = desc_state::unpack(
                    state_ref(*desc_at(static_cast<id_t>(seq))).load(std::memory_order_relaxed)
                ).id;

                auto snap = snapshot(id);
                if (snap.state == st_miss || snap.state == st_reserved || snap.seq != seq)
                    return std::unexpected { error::invalid_seq };

                if (snap.state == st_reusable || snap.data_begin == no_data)
                    return std::unexpected { error::data_lost };

                const info copy = *info_at(static_cast<id_t>(seq));

                if (!buf.empty())
                {
                    auto blk = block_at(snap.data_begin);
                    if (wraps(snap.data_begin, snap.data_end))
                        blk = block_at(0);

                    const auto num = std::min<std::size_t>(buf.size(), copy.len);
                    std::memcpy(buf.data(), blk->text, num);
                }

                snap = snapshot(id);
                if (snap.state == st_miss || snap.state == st_reserved || snap.seq != seq)
                    return std::unexpected { error::invalid_seq };
                if (snap.state == st_reusable)
                    return std::unexpected { error::data_lost };

                return copy;
            }

            static constexpr std::array<desc, desc_count> make_descs()
            {
                std::array<desc, desc_count> arr { };
                arr[desc_count - 1].state = desc_state {
                    .id = sentinel_id(),
                    .state = st_reusable
                } .pack();
                arr[desc_count - 1].data_begin = no_data;
                arr[desc_count - 1].data_end = no_data;
                return arr;
            }

            static constexpr std::array<info, desc_count> make_infos()
            {
                std::array<info, desc_count> arr { };
                arr[0].seq = -static_cast<std::uint64_t>(desc_count);
                return arr;
            }

            public:
            constexpr ring()
                : descs { make_descs() }, infos { make_infos() }, data { },
                  head_id { static_cast<std::size_t>(sentinel_id()) },
                  tail_id { static_cast<std::size_t>(sentinel_id()) },
                  head_lpos { 0 - data_size }, tail_lpos { 0 - data_size } { }

            std::expected<reservation, error> reserve(std::size_t size)
            {
                if (size == 0 || size > max_size)
                    return std::unexpected { error::too_large };

                reservation res { };
                res.prev_irqs = arch::int_switch_status(false);

                auto id = reserve_desc();
                if (!id)
                {
                    arch::int_switch(res.prev_irqs);
                    return std::unexpected { id.error() };
                }
                res.id = *id;

                auto desc = desc_at(res.id);
                auto info = info_at(res.id);
                res.meta = info;

                const auto seq = info->seq;
                if (seq == 0 && (static_cast<std::size_t>(res.id) & desc_mask) != 0)
                    info->seq = static_cast<std::uint64_t>(res.id) & desc_mask;
                else
                    info->seq = seq + desc_count;

                auto buf = alloc_data(size, res.id, desc->data_begin, desc->data_end);
                if (!buf)
                {
                    publish(res);
                    return std::unexpected { buf.error() };
                }
                res.buf = *buf;
                return res;
            }

            void publish(const reservation &res)
            {
                auto from = desc_state { .id = res.id, .state = st_reserved } .pack();
                const auto to = desc_state { .id = res.id, .state = st_finalised } .pack();
                state_ref(*desc_at(res.id)).compare_exchange_strong(
                    from, to,
                    std::memory_order_release,
                    std::memory_order_relaxed
                );

                arch::int_switch(res.prev_irqs);
            }

            std::uint64_t first_seq() const
            {
                while (true)
                {
                    const auto id = static_cast<id_t>(tail_id.load(std::memory_order_acquire));
                    const auto snap = snapshot(id);
                    if (snap.state == st_finalised || snap.state == st_reusable)
                        return snap.seq;
                    arch::pause();
                }
            }

            std::expected<info, error> read(std::uint64_t seq, std::span<char> buf = { })
            {
                auto cur = seq;
                while (true)
                {
                    auto res = read_one(cur, buf);
                    if (res)
                        return res;

                    switch (res.error())
                    {
                        case error::invalid_seq:
                        {
                            const auto first = first_seq();
                            if (cur < first)
                            {
                                cur = first;
                                continue;
                            }
                            return std::unexpected { error::not_yet_available };
                        }
                        case error::data_lost:
                        {
                            const auto first = first_seq();
                            cur = (cur < first) ? first : cur + 1;
                            continue;
                        }
                        default:
                            return res;
                    }
                }
            }
        };

        using ring_type = ring<14, 8>;
        constinit ring_type buffer { };

        constexpr std::size_t len_time = 18;
        constinit std::array<char, ring_type::max_size + len_time> data { };

        void prints(std::string_view str)
        {
            auto current = loggers;
            const auto internal_print = [&current](auto str)
            {
                std::string_view view { str };
                current->prints(view);
            };

            while (current)
            {
                current->lock();
                bool first = true;
                for (const auto seg : str | std::views::split('\n'))
                {
                    if (!first)
                        internal_print("\r\n");
                    first = false;
                    internal_print(seg);
                }
                current->unlock();
                current = current->next;
            }
        }

        void print(ring_type::info &info)
        {
            if (info.lvl != level::none)
            {
                auto nanos = info.time;
                const auto [h, m, s] = lib::time_from(nanos / 1'000'000'000);
                nanos %= 1'000'000'000;
                nanos /= 1'000;

                std::array<char, len_time> time { };
                lib::bug_on(fmt::format_to_n(
                    time.data(), time.size(), "[{:02}:{:02}:{:02}.{:06}] ",
                    h, m, s, nanos
                ).size != len_time);
                std::memcpy(data.data(), time.data(), len_time);

                const std::string_view view {
                    data.data(), info.len + len_time
                };
                lock.lock();
                prints(view);
                lock.unlock();
            }
            else
            {
                const std::string_view view {
                    data.data() + len_time, info.len
                };
                lock.lock();
                prints(view);
                lock.unlock();
            }
        }
    } // namespace

    void register_logger(logger *lg)
    {
        if (loggers)
        {
            lg->next = loggers;
            loggers = lg;
        }
        else loggers = lg;
    }

    void set_direct_print(bool _direct)
    {
        direct.store(_direct, std::memory_order_release);
    }

    // called from panic
    void force_unlock()
    {
        lock.unlock();

        auto current = loggers;
        while (current)
        {
            current->unlock();
            current = current->next;
        }

        while (true)
        {
            auto res = buffer.read(next_seq, std::span {
                data.data() + len_time, data.size() - len_time
            });
            if (!res.has_value())
                break;

            print(*res);
            next_seq = res->seq + 1;
        }
    }

    void wait_for_logs()
    {
        if (direct.load(std::memory_order_acquire))
            return;

        const auto target = last_published.load(std::memory_order_acquire);
        while (true)
        {
            const auto gen = finished.snapshot_gen();
            if (last_consumed.load(std::memory_order_acquire) >= target)
                break;
            finished.wait_prepared(gen);
        }
    }

    namespace detail
    {
        void vprint(bool add_nl, level lvl, std::size_t len, std::string_view fmt, fmt::format_args args)
        {
            auto nanos = chrono::now(chrono::monotonic).to_ns();

            const auto index = std::to_underlying(lvl);
            const auto prefix = prefixes[index];

            if (direct.load(std::memory_order_acquire))
            {
                lock.lock();

                std::size_t off = 0;
                if (lvl != level::none)
                {
                    const auto [h, m, s] = lib::time_from(nanos / 1'000'000'000);
                    nanos %= 1'000'000'000;
                    nanos /= 1'000;

                    off = fmt::format_to_n(
                        data.data(), data.size(),
                        "[{:02}:{:02}:{:02}.{:06}] {}", h, m, s, nanos, prefix
                    ).size;
                }

                off += fmt::vformat_to_n(
                    data.data() + off, data.size() - off, fmt, args
                ).size;

                if (add_nl)
                    data[off++] = '\n';

                prints(std::string_view { data.data(), off });

                lock.unlock();
            }
            else
            {
                if (add_nl)
                    len++;
                len += prefix.size();

                auto res = buffer.reserve(len);
                if (!res)
                {
                    dropped.fetch_add(1, std::memory_order_relaxed);
                    available.wake_all();
                    return;
                }

                if ((res->meta->lvl = lvl) != level::none)
                    res->meta->time = nanos;
                res->meta->len = len;

                std::strncpy(res->buf.data(), prefix.data(), prefix.size());
                fmt::vformat_to_n(res->buf.data() + prefix.size(), len - prefix.size(), fmt, args);
                if (add_nl)
                    res->buf[len - 1] = '\n';

                buffer.publish(*res);
                last_published.store(res->meta->seq, std::memory_order_release);
                available.wake_all();
            }
        }
    } // namespace detail

    namespace
    {
        void print_dropped(std::uint64_t count)
        {
            auto nanos = chrono::now(chrono::monotonic).to_ns();
            const auto [h, m, s] = lib::time_from(nanos / 1'000'000'000);
            nanos %= 1'000'000'000;
            nanos /= 1'000;

            std::array<char, 96> buf { };
            const auto prefix = prefixes[std::to_underlying(level::error)];
            const auto sz = fmt::format_to_n(
                buf.data(), buf.size(),
                "[{:02}:{:02}:{:02}.{:06}] {}log: dropped {} entries\n",
                h, m, s, nanos, prefix, count
            ).size;

            lock.lock();
            prints(std::string_view { buf.data(), sz });
            lock.unlock();
        }
    } // namespace

    void consumer()
    {
        set_direct_print(false);

        std::uint64_t reported_drops = 0;

        while (true)
        {
            const auto gen = available.snapshot_gen();
            auto res = buffer.read(next_seq, std::span {
                data.data() + len_time, data.size() - len_time
            });
            if (!res.has_value())
            {
                if (res.error() == ring_type::error::not_yet_available)
                {
                    const auto cur = dropped.load(std::memory_order_relaxed);
                    if (cur > reported_drops)
                    {
                        print_dropped(cur - reported_drops);
                        reported_drops = cur;
                    }
                    finished.wake_all();
                    available.wait_prepared(gen);
                    continue;
                }
                next_seq = buffer.first_seq();
                last_consumed.store(next_seq - 1, std::memory_order_release);
                continue;
            }

            if (res->seq != next_seq)
                dropped.fetch_add(res->seq - next_seq, std::memory_order_relaxed);

            const auto cur = dropped.load(std::memory_order_relaxed);
            if (cur > reported_drops)
            {
                print_dropped(cur - reported_drops);
                reported_drops = cur;
            }

            print(*res);

            next_seq = res->seq + 1;
            last_consumed.store(res->seq, std::memory_order_release);
        }
        std::unreachable();
    }

    lib::initgraph::task log_task
    {
        "log.create-thread",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            sched::pid0_created_stage()
        },
        [] {
            if (!lib::syscall::log_enabled)
                sched::spawn(consumer, 0, 5);
        }
    };

    extern "C" void putchar_(char chr)
    {
        char str[2] { chr, 0 };
        prints(str);
    }
} // namespace lib::log
