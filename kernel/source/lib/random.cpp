// Copyright (C) 2024-2026  ilobilo

module lib;

import system.cpu.local;
import system.chrono;
import arch;
import std;

namespace lib
{
    namespace
    {
        struct base_crng
        {
            blake2s_state pool;
            std::array<std::byte, chacha20_key_size> key;
            std::uint64_t generation;
            bool initialised;
        };

        struct local_crng
        {
            std::array<std::byte, chacha20_key_size> key;
            std::uint32_t counter;
            std::uint64_t generation;
            std::uint64_t bytes_since_derive;
            bool initialised;
        };

        struct irq_pool
        {
            std::array<std::uint64_t, 4> entries;
            std::uint32_t count;
        };

        template<typename Type>
        struct batch
        {
            std::array<Type, chacha20_block_size / sizeof(Type)> entropy;
            std::uint64_t generation;
            std::size_t position;
            bool initialised;
        };

        using batch_u32 = batch<std::uint32_t>;
        using batch_u64 = batch<std::uint64_t>;

        constexpr std::uint64_t reseed_interval = 1024 * 1024;
        constexpr std::array<std::byte, chacha20_nonce_size> zero_nonce { };

        spinlock_irq _base_lock;
        base_crng _base;
        std::atomic<std::uint64_t> _base_generation = 0;

        std::atomic<bool> _jitter_armed = false;

        cpu_local(local_crng, _local);
        cpu_local(irq_pool, _irq_pool);
        cpu_local(batch_u32, _batch_u32);
        cpu_local(batch_u64, _batch_u64);

        void memzero(void *ptr, std::size_t n)
        {
            std::memset(ptr, 0, n);
            cmb();
        }

        void mix_chrono_jitter_locked()
        {
            for (std::size_t i = 0; i < 32; i++)
            {
                const auto now = static_cast<std::uint64_t>(
                    chrono::now(chrono::realtime).to_ns()
                );
                blake2s_update(_base.pool, std::as_bytes(std::span { &now, 1 }));
            }
        }

        void mix_hw_rng_locked()
        {
            std::array<std::byte, 64> buf;
            if (const auto got = arch::hardware_random(buf); got > 0)
                blake2s_update(_base.pool, std::span { buf.data(), got });
        }

        void base_extract_locked()
        {
            std::array<std::byte, blake2s_hash_size> digest;
            blake2s_state snapshot = _base.pool;
            blake2s_final(snapshot, digest);

            for (std::size_t i = 0; i < chacha20_key_size; i++)
                _base.key[i] ^= digest[i];

            blake2s_init_key(_base.pool, blake2s_hash_size, _base.key);
            _base.generation++;
            _base_generation.store(_base.generation, std::memory_order_release);

            memzero(digest.data(), digest.size());
            memzero(&snapshot, sizeof(snapshot));
        }

        void base_initialise_locked()
        {
            blake2s_init(_base.pool, blake2s_hash_size);
            mix_chrono_jitter_locked();
            mix_hw_rng_locked();
            base_extract_locked();

            _base.initialised = true;
            _jitter_armed.store(true, std::memory_order_release);
        }

        void drain_irq_pool_locked()
        {
            auto &p = _irq_pool.unsafe_get();
            if (p.count == 0)
                return;

            blake2s_update(_base.pool, std::as_bytes(std::span { p.entries }));
            p.entries = { };
            p.count = 0;
        }

        void derive_local(local_crng &local)
        {
            const std::unique_lock _ { _base_lock };
            if (!_base.initialised)
                base_initialise_locked();

            drain_irq_pool_locked();

            std::array<std::byte, chacha20_block_size> block;
            chacha20_block(_base.key, zero_nonce, 0, block);
            std::copy_n(block.begin(), chacha20_key_size, _base.key.begin());
            std::copy_n(
                block.begin() + chacha20_key_size,
                chacha20_key_size,
                local.key.begin()
            );
            memzero(block.data(), block.size());

            local.counter = 0;
            local.bytes_since_derive = 0;
            local.generation = _base.generation;
            local.initialised = true;
        }

        void fill_local(local_crng &local, std::span<std::byte> out)
        {
            if (!local.initialised ||
                local.generation != _base_generation.load(std::memory_order_acquire) ||
                local.bytes_since_derive >= reseed_interval)
                derive_local(local);

            constexpr std::size_t output_per_block = chacha20_block_size - chacha20_key_size;
            while (!out.empty())
            {
                std::array<std::byte, chacha20_block_size> block;
                chacha20_block(local.key, zero_nonce, local.counter, block);
                local.counter++;

                std::copy_n(block.begin(), chacha20_key_size, local.key.begin());

                const std::size_t copylen = std::min(out.size(), output_per_block);
                std::copy_n(block.begin() + chacha20_key_size, copylen, out.begin());
                out = out.subspan(copylen);
                local.bytes_since_derive += copylen;

                memzero(block.data(), block.size());
            }
        }

        template<typename Type>
        Type pop_batch(batch<Type> &b)
        {
            const auto cur_gen = _base_generation.load(std::memory_order_acquire);
            if (!b.initialised || b.position >= b.entropy.size() || b.generation != cur_gen)
            {
                fill_local(_local.unsafe_get(), std::as_writable_bytes(std::span { b.entropy }));
                b.position = 0;
                b.generation = cur_gen;
                b.initialised = true;
            }

            const Type ret = b.entropy[b.position];
            b.entropy[b.position] = 0;
            b.position++;
            return ret;
        }
    } // namespace

    void add_entropy(std::span<const std::byte> data)
    {
        if (data.empty())
            return;

        const std::unique_lock _ { _base_lock };
        if (!_base.initialised)
            base_initialise_locked();
        blake2s_update(_base.pool, data);
        base_extract_locked();
    }

    std::uint32_t get_random_u32()
    {
        lock::acquire_irq();
        const auto ret = pop_batch(_batch_u32.unsafe_get());
        lock::release_irq();
        return ret;
    }

    std::uint64_t get_random_u64()
    {
        lock::acquire_irq();
        const auto ret = pop_batch(_batch_u64.unsafe_get());
        lock::release_irq();
        return ret;
    }

    void add_irq_jitter(std::size_t vector, std::uintptr_t ip)
    {
        if (!_jitter_armed.load(std::memory_order_acquire))
            return;

        auto &p = _irq_pool.unsafe_get();
        const auto mixed = arch::cycle_count() ^ (static_cast<std::uint64_t>(vector) << 32)
                         ^ static_cast<std::uint64_t>(ip);
        p.entries[p.count % p.entries.size()] ^= mixed;
        p.count++;
    }

    std::ssize_t random_bytes(maybe_uspan<std::byte> buffer)
    {
        if (buffer.is_user())
        {
            membuffer buf { std::min(buffer.size_bytes(), 1024uz) };
            std::size_t progress = 0;
            while (progress < buffer.size_bytes())
            {
                const auto chunk_size = std::min(
                    buffer.size_bytes() - progress, buf.size_bytes()
                );

                lock::acquire_irq();
                fill_local(_local.unsafe_get(), std::span { buf.data(), chunk_size });
                lock::release_irq();

                if (!buffer.subspan(progress, chunk_size).copy_from(buf.data()))
                    return -1;
                progress += chunk_size;
            }
            return static_cast<std::ssize_t>(progress);
        }

        lock::acquire_irq();
        fill_local(_local.unsafe_get(), buffer.span());
        lock::release_irq();
        return buffer.size_bytes();
    }
} // namespace lib
