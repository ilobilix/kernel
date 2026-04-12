// Copyright (C) 2024-2026  ilobilo

module;

#include <flanterm.h>

module lib;

import drivers.output.terminal;
import drivers.output.serial;
import system.sched.wait_queue;
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

        constinit lib::spinlock_irq lock;
        constinit std::atomic_bool direct = true;

        constinit logger *loggers = nullptr;

        // stolen from https://github.com/rdmsr/zag/blob/de98aab7f068221872f2b110d371c1037e2cfe15/src/kern/log_ring.zig
        template<std::size_t DataBits, std::size_t MsgBits>
        class ring
        {
            public:
            using id_t = unsigned _BitInt(62);

            enum class error
            {
                no_space,
                reservation_failed,
                invalid_desc,
                data_lost,
                not_yet_available,
                zero_size,
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
                bool irqs;
                id_t id;
                info *info_ptr;
                std::span<char> buf;
            };

            private:
            struct blk_pos { std::size_t begin, end; };

            struct alignas(sizeof(std::uint64_t)) desc_state
            {
                enum : int {
                    reserved = 0,
                    published = 1,
                    free = 2,
                    miss = 3
                };

                union {
                    struct {
                        id_t id : 62;
                        std::size_t state : 2;
                    };
                    std::uint64_t raw;
                };

                bool operator==(const desc_state &o) const { return raw == o.raw; }
            };
            static_assert(sizeof(desc_state) == 8);

            struct desc
            {
                alignas(sizeof(std::uint64_t)) mutable desc_state state;
                blk_pos text_pos;
            };

            struct data_block
            {
                alignas(sizeof(std::uint64_t)) std::uint64_t id;
                char data[];
            };

            static constexpr std::size_t lpos_no_data = 1;
            static constexpr std::size_t data_size = 1ul << DataBits;
            static constexpr std::size_t desc_bits = DataBits - MsgBits;
            static constexpr std::size_t desc_count = 1ul << desc_bits;
            static constexpr std::size_t desc_mask = desc_count - 1;
            static constexpr std::size_t data_mask = data_size - 1;

            std::array<desc, desc_count> descs;
            std::array<info, desc_count> infos;
            std::array<std::byte, data_size> data;

            std::atomic_size_t head_id;
            std::atomic_size_t tail_id;
            std::atomic_size_t head_lpos;
            std::atomic_size_t tail_lpos;

            static constexpr std::atomic_ref<desc_state> state_ref(const desc &d)
            {
                return std::atomic_ref<desc_state> { d.state };
            }

            int get_desc_state(id_t id, desc_state s) const
            {
                if (id != s.id)
                    return desc_state::miss;
                return static_cast<int>(s.state);
            }

            static id_t prev_wrap(id_t id)
            {
                return id - static_cast<id_t>(desc_count);
            }

            static std::size_t to_block_size(std::size_t size)
            {
                constexpr auto a = sizeof(std::size_t);
                return (size + sizeof(data_block) + a - 1u) & ~(a - 1u);
            }

            desc *to_desc(id_t id)
            {
                return &descs[id & desc_mask];
            }

            const desc *to_desc(id_t id) const
            {
                return &descs[id & desc_mask];
            }

            info *to_info(id_t id)
            {
                return &infos[id & desc_mask];
            }

            const info *to_info(id_t id) const
            {
                return &infos[id & desc_mask];
            }

            data_block *to_block(std::size_t lpos)
            {
                return std::launder(reinterpret_cast<data_block *>(&data[lpos & data_mask]));
            }

            static bool check_size(std::size_t size)
            {
                return size <= data_size / 2;
            }

            static bool need_more_space(std::size_t cur, std::size_t nxt)
            {
                return nxt - cur - 1 < data_size;
            }

            static bool data_wraps(std::size_t begin, std::size_t end)
            {
                return (begin >> DataBits) != ((end - 1) >> DataBits);
            }

            static std::size_t get_next_lpos(std::size_t lpos, std::size_t size)
            {
                const auto next = lpos + size;
                if (data_wraps(lpos, next))
                    return ((next >> DataBits) << DataBits) + size;
                return next;
            }

            struct read_desc_res
            {
                int state;
                std::uint64_t seq;
                desc desc_copy;
            };

            read_desc_res read_desc(id_t id) const
            {
                const auto d = to_desc(id);
                const auto inf = to_info(id);

                auto dstate = state_ref(*d).load(std::memory_order_acquire);
                const auto state = get_desc_state(id, dstate);

                read_desc_res ret { .state = state, .seq = 0, .desc_copy = { } };

                if (state == desc_state::miss || state == desc_state::reserved)
                {
                    ret.desc_copy.state = dstate;
                    return ret;
                }

                ret.desc_copy.text_pos = d->text_pos;
                ret.desc_copy.state = dstate;
                ret.seq = inf->seq;

                rmb();

                dstate = state_ref(*d).load(std::memory_order_acquire);
                ret.state = get_desc_state(id, dstate);
                ret.desc_copy.state = dstate;

                return ret;
            }

            std::expected<void, error> read_desc_finalised(id_t id, std::uint64_t seq, desc &out) const
            {
                const auto res = read_desc(id);
                out = res.desc_copy;

                if (res.state == desc_state::miss || res.state == desc_state::reserved || res.seq != seq)
                    return std::unexpected { error::invalid_desc };

                if (res.state == desc_state::free || out.text_pos.begin == lpos_no_data)
                    return std::unexpected { error::data_lost };

                return { };
            }

            void free_desc(id_t id)
            {
                auto d = to_desc(id);
                desc_state expected { .id = id, .state = desc_state::published };
                const desc_state nxt { .id = id, .state = desc_state::free };

                state_ref(*d).compare_exchange_strong(
                    expected, nxt,
                    std::memory_order_release,
                    std::memory_order_relaxed
                );
            }

            std::optional<std::size_t> free_data(std::size_t begin, std::size_t end)
            {
                std::size_t cur = begin;
                while (need_more_space(cur, end))
                {
                    const auto blk = to_block(cur);
                    const id_t id = static_cast<id_t>(
                        std::atomic_ref<std::uint64_t> { blk->id }
                            .load(std::memory_order_relaxed)
                    );
                    const auto res = read_desc(id);
                    const auto &d = res.desc_copy;

                    switch (res.state)
                    {
                        case desc_state::miss:
                        case desc_state::reserved:
                            return std::nullopt;
                        case desc_state::published:
                            if (d.text_pos.begin != cur)
                                return std::nullopt;
                            free_desc(id);
                            break;
                        case desc_state::free:
                            if (d.text_pos.begin != cur)
                                return std::nullopt;
                            break;
                    }

                    cur = d.text_pos.end;
                }

                return cur;
            }

            std::expected<void, error> data_advance_tail(std::size_t new_tail_lpos)
            {
                if (new_tail_lpos & 1)
                    return { };

                auto tail = tail_lpos.load(std::memory_order_relaxed);
                while (need_more_space(tail, new_tail_lpos))
                {
                    if (const auto next = free_data(tail, new_tail_lpos))
                    {
                        if (tail_lpos.compare_exchange_weak(tail, *next,
                            std::memory_order_release, std::memory_order_relaxed))
                            break;
                    }
                    else
                    {
                        const auto cur = tail_lpos.load(std::memory_order_acquire);
                        if (cur == tail)
                            return std::unexpected { error::reservation_failed };
                        tail = cur;
                    }
                    arch::pause();
                }

                return { };
            }

            std::expected<void, error> advance_tail(id_t tid)
            {
                const auto res = read_desc(tid);
                const auto &tdesc = res.desc_copy;

                switch (res.state)
                {
                    case desc_state::published:
                        free_desc(tid);
                        break;
                    case desc_state::reserved:
                        return std::unexpected { error::reservation_failed };

                    case desc_state::miss:
                        if (tdesc.state.id == prev_wrap(tid))
                            return std::unexpected { error::reservation_failed };
                        return { };

                    case desc_state::free:
                        break;
                }

                if (const auto ret = data_advance_tail(tdesc.text_pos.end); !ret)
                    return ret;

                const auto next = read_desc(static_cast<id_t>(tid + 1));
                if (next.state == desc_state::published || next.state == desc_state::free)
                {
                    std::size_t tmp = static_cast<std::size_t>(tid);
                    tail_id.compare_exchange_strong(
                        tmp, static_cast<std::size_t>(tid + 1),
                        std::memory_order_release, std::memory_order_relaxed
                    );
                }
                else
                {
                    const auto cur = tail_id.load(std::memory_order_acquire);
                    if (cur != static_cast<std::size_t>(tid))
                        return { };
                    return std::unexpected { error::reservation_failed };
                }

                return { };
            }

            std::expected<id_t, error> reserve_desc()
            {
                auto head = static_cast<id_t>(head_id.load(std::memory_order_acquire));
                id_t new_id;

                while (true)
                {
                    new_id = head + 1;
                    const id_t prev_id = prev_wrap(new_id);

                    const id_t cur_tail = static_cast<id_t>(tail_id.load(std::memory_order_acquire));
                    if (prev_id == cur_tail)
                    {
                        if (const auto ret = advance_tail(prev_id); !ret)
                            return std::unexpected { ret.error() };
                    }

                    auto tmp = static_cast<std::size_t>(head);
                    if (head_id.compare_exchange_weak(tmp, static_cast<std::size_t>(new_id),
                        std::memory_order_acq_rel, std::memory_order_acquire))
                        break;

                    head = static_cast<id_t>(tmp);
                    arch::pause();
                }

                auto d = to_desc(new_id);
                auto prev_state = state_ref(*d).load(std::memory_order_relaxed);

                if (prev_state.raw != 0 && get_desc_state(prev_wrap(new_id), prev_state) != desc_state::free)
                    return std::unexpected { error::reservation_failed };

                const desc_state val { .id = new_id, .state = desc_state::reserved };
                if (!state_ref(*d).compare_exchange_strong(prev_state, val,
                    std::memory_order_release, std::memory_order_relaxed))
                    return std::unexpected { error::reservation_failed };

                return new_id;
            }

            std::expected<std::span<char>, error> alloc_data(std::size_t size, id_t id, blk_pos &out_pos)
            {
                if (size == 0)
                {
                    out_pos = { lpos_no_data, lpos_no_data };
                    return std::unexpected { error::zero_size };
                }

                const std::size_t blk_size = to_block_size(size);
                auto head = head_lpos.load(std::memory_order_relaxed);

                while (true)
                {
                    const auto new_head = get_next_lpos(head, blk_size);

                    if (const auto ret = data_advance_tail(new_head - data_size); !ret)
                    {
                        out_pos = { lpos_no_data, lpos_no_data };
                        return std::unexpected { ret.error() };
                    }

                    if (head_lpos.compare_exchange_weak(head, new_head,
                            std::memory_order_acq_rel, std::memory_order_acquire))
                    {
                        auto blk = to_block(head);
                        std::atomic_ref<std::uint64_t> { blk->id }
                            .store(static_cast<std::uint64_t>(id), std::memory_order_relaxed);

                        if (data_wraps(head, new_head))
                        {
                            blk = to_block(0);
                            std::atomic_ref<std::uint64_t> { blk->id }
                                .store(static_cast<std::uint64_t>(id), std::memory_order_relaxed);
                        }

                        out_pos = { head, new_head };
                        return std::span<char>(blk->data, size);
                    }
                    arch::pause();
                }
            }

            std::expected<info, error> read_internal(std::uint64_t seq, std::span<char> buf)
            {
                const id_t id = state_ref(*to_desc(static_cast<id_t>(seq)))
                    .load(std::memory_order_relaxed).id;

                desc d;
                if (const auto ret = read_desc_finalised(id, seq, d); !ret)
                    return std::unexpected { ret.error() };

                const info result = *to_info(static_cast<id_t>(seq));

                if (!buf.empty())
                {
                    if (d.text_pos.begin == lpos_no_data)
                        return std::unexpected { error::data_lost };

                    auto blk = to_block(d.text_pos.begin);
                    if (data_wraps(d.text_pos.begin, d.text_pos.end))
                        blk = to_block(0);

                    const std::size_t n = std::min(buf.size(), static_cast<std::size_t>(result.len));
                    std::memcpy(buf.data(), blk->data, n);
                }

                if (const auto ret = read_desc_finalised(id, seq, d); !ret)
                    return std::unexpected { ret.error() };

                return result;
            }

            static constexpr id_t dummy_id()
            {
                return static_cast<id_t>(0) - static_cast<id_t>(desc_count + 1);
            }

            static constexpr std::array<desc, desc_count> make_descs()
            {
                std::array<desc, desc_count> arr { };
                arr[desc_count - 1].state = { .id = dummy_id(), .state = desc_state::free };
                arr[desc_count - 1].text_pos = { lpos_no_data, lpos_no_data };
                return arr;
            }

            static constexpr std::array<info, desc_count> make_infos()
            {
                std::array<info, desc_count> arr { };
                arr[0].seq = -static_cast<std::uint64_t>(desc_count);
                return arr;
            }

            public:
            static constexpr std::size_t max_size = data_size / 2;

            constexpr ring() : descs { make_descs() }, infos { make_infos() }, data { },
                head_id { static_cast<std::size_t>(dummy_id()) },
                tail_id { static_cast<std::size_t>(dummy_id()) },
                head_lpos { static_cast<std::size_t>(-data_size) },
                tail_lpos { static_cast<std::size_t>(-data_size) } { }

            std::expected<reservation, error> reserve(std::size_t size)
            {
                if (!check_size(size))
                    return std::unexpected { error::no_space };

                reservation res { };
                res.irqs = arch::int_switch_status(false);

                auto id_res = reserve_desc();
                if (!id_res)
                {
                    arch::int_switch(res.irqs);
                    return std::unexpected { id_res.error() };
                }

                res.id = *id_res;

                auto d = to_desc(res.id);
                auto inf = to_info(res.id);

                res.info_ptr = inf;

                const auto seq = inf->seq;
                if (seq == 0 && (static_cast<std::size_t>(res.id) & desc_mask) != 0)
                    inf->seq = static_cast<std::uint64_t>(res.id) & desc_mask;
                else
                    inf->seq = seq + desc_count;

                auto buf_res = alloc_data(size, res.id, d->text_pos);
                if (!buf_res)
                {
                    publish(res);
                    return std::unexpected { buf_res.error() };
                }

                res.buf = *buf_res;
                return res;
            }

            void publish(const reservation &res)
            {
                auto d = to_desc(res.id);
                desc_state expected { .id = res.id, .state = desc_state::reserved  };
                const desc_state pub { .id = res.id, .state = desc_state::published };

                state_ref(*d).compare_exchange_strong(
                    expected, pub,
                    std::memory_order_release,
                    std::memory_order_relaxed
                );

                arch::int_switch(res.irqs);
            }

            std::uint64_t first_seq()
            {
                while (true)
                {
                    const id_t id  = static_cast<id_t>(tail_id.load(std::memory_order_acquire));
                    const auto res = read_desc(id);

                    if (res.state == desc_state::published || res.state == desc_state::free)
                        return res.seq;

                    arch::pause();
                }
            }

            std::expected<info, error> read(std::uint64_t seq, std::span<char> buf = { })
            {
                auto cur = seq;
                while (true)
                {
                    auto result = read_internal(cur, buf);
                    if (result)
                        return result;

                    switch (result.error())
                    {
                        case error::invalid_desc:
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
                            return result;
                    }
                }
            }
        };

        using ring_type = ring<14, 6>;
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
        while (last_consumed.load(std::memory_order_acquire) < target)
            finished.wait();
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

                const auto index = std::to_underlying(lvl);
                const auto prefix = prefixes[index];
                len += prefix.size();

                auto res = buffer.reserve(len);
                if (!res)
                    return;

                if ((res->info_ptr->lvl = lvl) != level::none)
                    res->info_ptr->time = nanos;
                res->info_ptr->len = len;

                std::strncpy(res->buf.data(), prefix.data(), prefix.size());
                fmt::vformat_to_n(res->buf.data() + prefix.size(), len - prefix.size(), fmt, args);
                if (add_nl)
                    res->buf[len - 1] = '\n';

                buffer.publish(*res);
                last_published.store(res->info_ptr->seq, std::memory_order_release);
                available.wake_all();
            }
        }
    } // namespace detail

    void consumer()
    {
        set_direct_print(false);

        while (true)
        {
            auto res = buffer.read(next_seq, std::span {
                data.data() + len_time, data.size() - len_time
            });
            if (!res.has_value())
            {
                if (res.error() == ring_type::error::not_yet_available)
                {
                    finished.wake_all();
                    available.wait_unint();
                    continue;
                }
                next_seq = buffer.first_seq();
                last_consumed.store(next_seq - 1, std::memory_order_release);
                continue;
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
#if !ILOBILIX_SYSCALL_LOG
            sched::spawn(consumer);
#endif
        }
    };

    extern "C" void putchar_(char chr)
    {
        char str[2] { chr, 0 };
        prints(str);
    }
} // namespace lib::log
