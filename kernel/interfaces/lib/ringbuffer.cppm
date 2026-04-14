// Copyright (C) 2024-2025  ilobilo

export module lib:ringbuffer;

import :memory;
import :math;
import :bug_on;
import std;

export namespace lib
{
    enum class rb_mode
    {
        overwrite,
        discard
    };

    template<typename Type, std::size_t Cap, rb_mode Mode, bool MultiProducer = false, bool MultiConsumer = true>
    class ringbuffer
    {
        static_assert(Mode == rb_mode::overwrite || Mode == rb_mode::discard, "invalid rb_mode");
        static_assert((Cap != 0) && ((Cap & (Cap - 1)) == 0), "capacity must be a power of 2");

        public:
        static inline constexpr std::size_t capacity = Cap;
        static inline constexpr rb_mode mode = Mode;
        static inline constexpr bool multi_producer = MultiProducer;
        static inline constexpr bool multi_consumer = MultiConsumer;

        using value_type = Type;

        private:
        static inline constexpr std::size_t mask = capacity - 1;
        static inline constexpr std::size_t alignment = std::hardware_destructive_interference_size;

        struct cell_t
        {
            std::atomic<std::size_t> sequence;
            alignas(value_type) std::byte data[sizeof(value_type)];
        };

        struct alignas(alignment)
        {
            std::conditional_t<multi_producer, std::atomic<std::size_t>, std::size_t> value;
        } _head;

        static inline constexpr bool tail_is_atomic = multi_consumer || mode == rb_mode::overwrite;
        struct alignas(alignment)
        {
            std::conditional_t<tail_is_atomic, std::atomic<std::size_t>, std::size_t> value;
        } _tail;

        cell_t *_storage;

        template<typename Func>
        std::pair<std::size_t, bool> _emplace_batch(std::size_t count, Func &&func)
        {
            bug_on(count == 0 || count > capacity, "ringbuffer::_emplace_batch(): invalid count");

            if constexpr (multi_producer)
            {
                bool overwritten = false;
                auto pos = _head.value.load(std::memory_order_relaxed);

                while (true)
                {
                    auto &cell_first = _storage[pos & mask];
                    const auto seq_first = cell_first.sequence.load(std::memory_order_acquire);
                    const auto dif_first = static_cast<std::ssize_t>(seq_first) - static_cast<std::ssize_t>(pos);

                    auto &cell_last = _storage[(pos + count - 1) & mask];
                    const auto seq_last = cell_last.sequence.load(std::memory_order_acquire);
                    const auto dif_last = static_cast<std::ssize_t>(seq_last) - static_cast<std::ssize_t>(pos + count - 1);

                    if (dif_first == 0 && dif_last == 0)
                    {
                        if (_head.value.compare_exchange_weak(pos, pos + count, std::memory_order_relaxed))
                        {
                            for (std::size_t i = 0; i < count; i++)
                            {
                                auto &cell = _storage[(pos + i) & mask];
                                func(i, reinterpret_cast<void *>(cell.data));
                                cell.sequence.store(pos + i + 1, std::memory_order_release);
                            }
                            return { count, overwritten };
                        }
                    }
                    else if (dif_first < 0 || dif_last < 0)
                    {
                        if constexpr (mode == rb_mode::overwrite)
                        {
                            auto current_tail = _tail.value.load(std::memory_order_relaxed);
                            auto target_tail = pos + count > capacity ? pos + count - capacity : 0;

                            if (static_cast<std::ssize_t>(current_tail) < static_cast<std::ssize_t>(target_tail))
                            {
                                if (_tail.value.compare_exchange_weak(current_tail, target_tail, std::memory_order_relaxed))
                                {
                                    overwritten = true;
                                    for (std::size_t i = current_tail; i < target_tail; i++)
                                    {
                                        auto &tail_cell = _storage[i & mask];
                                        reinterpret_cast<value_type *>(tail_cell.data)->~value_type();
                                        tail_cell.sequence.store(i + mask + 1, std::memory_order_release);
                                    }
                                }
                            }
                            pos = _head.value.load(std::memory_order_relaxed);
                        }
                        else return { 0, true };
                    }
                    else pos = _head.value.load(std::memory_order_relaxed);
                }
            }
            else
            {
                const auto pos = _head.value;
                bool overwritten = false;

                std::size_t current_tail;
                if constexpr (tail_is_atomic)
                    current_tail = _tail.value.load(std::memory_order_relaxed);
                else
                    current_tail = _tail.value;

                const std::size_t avail = capacity - (pos - current_tail);
                if (count > avail)
                {
                    if constexpr (mode == rb_mode::discard)
                        return { 0, true };

                    lib::bug_on(mode != rb_mode::overwrite);

                    std::size_t needed = count - avail;
                    if constexpr (tail_is_atomic)
                    {
                        auto t = current_tail;
                        while (true)
                        {
                            if (_tail.value.compare_exchange_weak(t, t + needed, std::memory_order_relaxed))
                            {
                                for (std::size_t i = 0; i < needed; i++)
                                {
                                    auto &tail_cell = _storage[(t + i) & mask];
                                    reinterpret_cast<value_type *>(tail_cell.data)->~value_type();
                                    tail_cell.sequence.store(t + i + mask + 1, std::memory_order_release);
                                }
                                overwritten = true;
                                break;
                            }
                            const auto new_avail = capacity - (pos - t);
                            if (count <= new_avail)
                                break;
                            needed = count - new_avail;
                        }
                    }
                }

                for (std::size_t i = 0; i < count; i++)
                {
                    auto &cell = _storage[(pos + i) & mask];
                    func(i, reinterpret_cast<void *>(cell.data));
                    cell.sequence.store(pos + i + 1, std::memory_order_release);
                }
                _head.value += count;

                return { count, overwritten };
            }
        }

        template<typename Func>
        bool _emplace(Func &&func)
        {
            const auto [_, ovr_dsc] = _emplace_batch(1, [&](std::size_t, void *slot) { func(slot); });
            return ovr_dsc;
        }

        template<typename Func>
        std::size_t _pop_batch(std::size_t max_count, Func &&func)
        {
            bug_on(max_count == 0 || max_count > capacity, "ringbuffer::_pop_batch(): invalid count");

            if constexpr (tail_is_atomic)
            {
                auto pos = _tail.value.load(std::memory_order_relaxed);
                while (true)
                {
                    std::size_t available = 0;
                    bool retry = false;
                    for (std::size_t i = 0; i < max_count; i++)
                    {
                        auto &cell = _storage[(pos + i) & mask];
                        const auto seq = cell.sequence.load(std::memory_order_acquire);
                        const auto dif = static_cast<std::ssize_t>(seq) - static_cast<std::ssize_t>(pos + i + 1);

                        if (dif == 0)
                            available++;
                        else if (dif > 0)
                        {
                            pos = _tail.value.load(std::memory_order_relaxed);
                            retry = true;
                            break;
                        }
                        else break;
                    }

                    if (retry)
                        continue;

                    if (available == 0)
                        return 0;

                    if (_tail.value.compare_exchange_weak(pos, pos + available, std::memory_order_relaxed))
                    {
                        for (std::size_t i = 0; i < available; i++)
                        {
                            auto &cell = _storage[(pos + i) & mask];
                            func(i, reinterpret_cast<void *>(cell.data));
                            reinterpret_cast<value_type *>(cell.data)->~value_type();
                            cell.sequence.store(pos + i + mask + 1, std::memory_order_release);
                        }
                        return available;
                    }
                }
            }
            else
            {
                std::size_t pos = _tail.value;
                std::size_t count = 0;

                for (std::size_t i = 0; i < max_count; i++)
                {
                    auto &cell = _storage[(pos + i) & mask];
                    const auto seq = cell.sequence.load(std::memory_order_acquire);
                    const auto dif = static_cast<std::ssize_t>(seq) - static_cast<std::ssize_t>(pos + i + 1);

                    if (dif != 0)
                        break;

                    func(i, reinterpret_cast<void *>(cell.data));
                    reinterpret_cast<value_type *>(cell.data)->~value_type();
                    cell.sequence.store(pos + i + mask + 1, std::memory_order_release);
                    count++;
                }

                if (count > 0)
                    _tail.value += count;

                return count;
            }
        }

        public:
        ringbuffer() : _storage { lib::allocz<cell_t *>(sizeof(cell_t) * capacity) }
        {
            for (std::size_t i = 0; i < capacity; i++)
                new (&_storage[i].sequence) std::atomic<std::size_t> { i };

            if constexpr (multi_producer)
                _head.value.store(0, std::memory_order_relaxed);
            else
                _head.value = 0;
            if constexpr (tail_is_atomic)
                _tail.value.store(0, std::memory_order_relaxed);
            else
                _tail.value = 0;
        }

        ~ringbuffer()
        {
            if (!_storage)
                return;

            std::size_t head;
            if constexpr (multi_producer)
                head = _head.value.load(std::memory_order_relaxed);
            else
                head = _head.value;
            std::size_t tail;
            if constexpr (tail_is_atomic)
                tail = _tail.value.load(std::memory_order_relaxed);
            else
                tail = _tail.value;

            while (tail < head)
            {
                auto &cell = _storage[tail & mask];
                reinterpret_cast<value_type *>(cell.data)->~value_type();
                tail++;
            }

            lib::free(_storage);
        }

        ringbuffer(const ringbuffer &) = delete;
        ringbuffer(ringbuffer &&) = delete;

        ringbuffer &operator=(const ringbuffer &) = delete;
        ringbuffer &operator=(ringbuffer &&) = delete;

        bool push(const value_type &value)
        {
            return _emplace([&](void *slot) {
                new (slot) value_type { value };
            });
        }

        bool push(value_type &&value)
        {
            return _emplace([&](void *slot) {
                new (slot) value_type { std::move(value) };
            });
        }

        std::size_t push(std::span<const value_type> values)
        {
            const auto [cnt, _] = _emplace_batch(values.size(), [&](std::size_t i, void *slot) {
                new (slot) value_type { values[i] };
            });
            return cnt;
        }

        std::size_t push(const value_type *values, std::size_t count)
        {
            return push(std::span<const value_type> { values, count });
        }

        template<typename... Args>
        bool emplace(Args &&...args)
        {
            return _emplace([&](void *slot) {
                new (slot) value_type { std::forward<Args>(args)... };
            });
        }

        std::optional<value_type> pop()
        {
            std::optional<value_type> ret { };
            const auto cnt = _pop_batch(1, [&](std::size_t, void *slot) {
                ret.emplace(std::move(*reinterpret_cast<value_type *>(slot)));
            });
            if (cnt == 0)
                bug_on(ret.has_value());
            return ret;
        }

        std::size_t pop(std::span<value_type> out)
        {
            return _pop_batch(out.size(), [&](std::size_t i, void *slot) {
                out[i] = std::move(*reinterpret_cast<value_type *>(slot));
            });
        }

        std::size_t size() const
        {
            std::size_t head;
            if constexpr (multi_producer)
                head = _head.value.load(std::memory_order_relaxed);
            else
                head = _head.value;
            std::size_t tail;
            if constexpr (tail_is_atomic)
                tail = _tail.value.load(std::memory_order_relaxed);
            else
                tail = _tail.value;
            return head - tail;
        }

        std::size_t length() const { return size(); }

        std::size_t available() const { return capacity - size(); }

        bool empty() const { return size() == 0; }
        bool full() const { return size() >= capacity; }
    };

    template<typename Type, std::size_t Cap>
    using rbspsco = ringbuffer<Type, Cap, rb_mode::overwrite, false, false>;
    template<typename Type, std::size_t Cap>
    using rbspscd = ringbuffer<Type, Cap, rb_mode::discard, false, false>;

    template<typename Type, std::size_t Cap>
    using rbspmco = ringbuffer<Type, Cap, rb_mode::overwrite, false, true>;
    template<typename Type, std::size_t Cap>
    using rbspmcd = ringbuffer<Type, Cap, rb_mode::discard, false, true>;

    template<typename Type, std::size_t Cap>
    using rbmpsco = ringbuffer<Type, Cap, rb_mode::overwrite, true, false>;
    template<typename Type, std::size_t Cap>
    using rbmpscd = ringbuffer<Type, Cap, rb_mode::discard, true, false>;

    template<typename Type, std::size_t Cap>
    using rbmpmco = ringbuffer<Type, Cap, rb_mode::overwrite, true, true>;
    template<typename Type, std::size_t Cap>
    using rbmpmcd = ringbuffer<Type, Cap, rb_mode::discard, true, true>;
} // export namespace lib