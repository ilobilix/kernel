// Copyright (C) 2024-2025  ilobilo

export module lib:ringbuffer;

import :memory;
import :math;
import :bug_on;
import cppstd;

export namespace lib
{
    enum class rb_mode
    {
        overwrite,
        discard
    };

    enum class rb_error
    {
        none,
        full,
        empty
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
        auto _emplace(Func &&func) -> std::expected<std::conditional_t<Mode == rb_mode::overwrite, bool, void>, rb_error>
        {
            if constexpr (multi_producer)
            {
                bool overwritten = false;

                auto pos = _head.value.load(std::memory_order_relaxed);
                while (true)
                {
                    auto &cell = _storage[pos & mask];
                    const auto seq = cell.sequence.load(std::memory_order_acquire);

                    const auto dif = static_cast<std::ssize_t>(seq) - static_cast<std::ssize_t>(pos);
                    if (dif == 0)
                    {
                        if (_head.value.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                        {
                            func(reinterpret_cast<void *>(cell.data));
                            cell.sequence.store(pos + 1, std::memory_order_release);

                            if constexpr (Mode == rb_mode::overwrite)
                                return overwritten;
                            else
                                return { };
                        }
                    }
                    else if (dif < 0)
                    {
                        if constexpr (mode == rb_mode::overwrite)
                        {
                            auto current_tail = _tail.value.load(std::memory_order_relaxed);
                            const auto diff = static_cast<std::ssize_t>(pos) - static_cast<std::ssize_t>(current_tail);
                            if (diff >= static_cast<std::ssize_t>(capacity))
                            {
                                if (_tail.value.compare_exchange_weak(current_tail, current_tail + 1, std::memory_order_relaxed))
                                {
                                    auto &tail_cell = _storage[current_tail & mask];
                                    reinterpret_cast<value_type *>(tail_cell.data)->~value_type();
                                    tail_cell.sequence.store(current_tail + mask + 1, std::memory_order_release);
                                    overwritten = true;
                                }
                            }
                            pos = _head.value.load(std::memory_order_relaxed);
                        }
                        else return std::unexpected { rb_error::full };
                    }
                    else pos = _head.value.load(std::memory_order_relaxed);
                }
            }
            else
            {
                auto pos = _head.value;
                auto &cell = _storage[pos & mask];
                const auto seq = cell.sequence.load(std::memory_order_acquire);

                bool overwritten = false;

                const auto dif = static_cast<std::ssize_t>(seq) - static_cast<std::ssize_t>(pos);
                if (dif != 0)
                {
                    if constexpr (mode == rb_mode::discard)
                        return std::unexpected { rb_error::full };

                    lib::bug_on(mode != rb_mode::overwrite);
                    lib::bug_on(dif > 0);

                    if constexpr (tail_is_atomic)
                    {
                        auto current_tail = _tail.value.load(std::memory_order_relaxed);
                        if (pos - current_tail >= capacity)
                        {
                            if (_tail.value.compare_exchange_strong(current_tail, current_tail + 1, std::memory_order_acq_rel))
                            {
                                auto &tail_cell = _storage[current_tail & mask];
                                reinterpret_cast<value_type *>(tail_cell.data)->~value_type();
                                tail_cell.sequence.store(current_tail + mask + 1, std::memory_order_release);
                                overwritten = true;
                            }
                        }
                    }
                    else
                    {
                        const auto current_tail = _tail.value;

                        auto &tail_cell = _storage[current_tail & mask];
                        reinterpret_cast<value_type *>(tail_cell.data)->~value_type();

                        _tail.value++;
                        tail_cell.sequence.store(current_tail + mask + 1, std::memory_order_release);

                        overwritten = true;
                    }
                }

                func(reinterpret_cast<void *>(cell.data));
                _head.value++;
                cell.sequence.store(pos + 1, std::memory_order_release);

                if constexpr (mode == rb_mode::overwrite)
                    return overwritten;
                else
                    return { };
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

        auto push(const value_type &value)
        {
            return _emplace([&](void *slot) {
                new (slot) value_type { value };
            });
        }

        auto push(value_type &&value)
        {
            return _emplace([&](void *slot) {
                new (slot) value_type { std::move(value) };
            });
        }

        template<typename... Args>
        auto emplace(Args &&...args)
        {
            return _emplace([&](void *slot) {
                new (slot) value_type { std::forward<Args>(args)... };
            });
        }

        std::expected<value_type, rb_error> pop()
        {
            if constexpr (tail_is_atomic)
            {
                auto pos = _tail.value.load(std::memory_order_relaxed);
                while (true)
                {
                    auto &cell = _storage[pos & mask];
                    const auto seq = cell.sequence.load(std::memory_order_acquire);

                    const auto dif = static_cast<std::ssize_t>(seq) - static_cast<std::ssize_t>(pos + 1);
                    if (dif == 0)
                    {
                        if (_tail.value.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                        {
                            auto *ptr = reinterpret_cast<value_type *>(cell.data);
                            auto value = std::move(*ptr);
                            ptr->~value_type();
                            cell.sequence.store(pos + mask + 1, std::memory_order_release);
                            return value;
                        }
                    }
                    else if (dif > 0)
                        pos = _tail.value.load(std::memory_order_relaxed);
                    else
                        return std::unexpected { rb_error::empty };
                }
            }
            else
            {
                std::size_t pos = _tail.value;

                auto &cell = _storage[pos & mask];
                const auto seq = cell.sequence.load(std::memory_order_acquire);

                const auto dif = static_cast<std::ssize_t>(seq) - static_cast<std::ssize_t>(pos + 1);
                if (dif != 0)
                    return std::unexpected { rb_error::empty };

                auto *ptr = reinterpret_cast<value_type *>(cell.data);
                auto value = std::move(*ptr);
                ptr->~value_type();

                _tail.value++;

                cell.sequence.store(pos + mask + 1, std::memory_order_release);
                return value;
            }
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