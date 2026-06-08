// Copyright (C) 2024-2026  ilobilo

module system.random;

import system.chrono;
import system.sched;
import system.cpu;
import system.cpu.local;
import boot;
import arch;

namespace random
{
    namespace
    {
        struct base_crng
        {
            lib::blake2s_state pool;
            std::array<std::byte, lib::chacha20_key_size> key;
            std::uint64_t generation;
            bool initialised;
        };

        struct local_crng
        {
            std::array<std::byte, lib::chacha20_key_size> key;
            std::uint32_t counter;
            std::uint64_t generation;
            std::uint64_t bytes_since_derive;
            bool initialised;
        };

        struct irq_pool
        {
            std::array<std::uint64_t, 4> entries;
        };

        template<typename Type>
        struct batch
        {
            std::array<Type, lib::chacha20_block_size / sizeof(Type)> entropy;
            std::uint64_t generation;
            std::size_t position;
            bool initialised;
        };

        using batch_u32 = batch<std::uint32_t>;
        using batch_u64 = batch<std::uint64_t>;

        constexpr std::uint64_t reseed_interval = 1024 * 1024;
        constexpr std::uint64_t reseed_period_ns = 60'000'000'000ull;
        constexpr std::array<std::byte, lib::chacha20_nonce_size> zero_nonce { };

        base_crng _base;
        lib::spinlock_irq _base_lock;
        std::atomic<std::uint64_t> _base_generation = 0;

        std::atomic<bool> _jitter_armed = false;

        cpu_local(local_crng, _local);
        cpu_local(irq_pool, _irq_pool);
        cpu_local(batch_u32, _batch_u32);
        cpu_local(batch_u64, _batch_u64);

        void memzero(void *ptr, std::size_t n)
        {
            std::memset(ptr, 0, n);
            lib::cmb();
        }

        constexpr std::uint64_t rotl64(std::uint64_t x, int k)
        {
            return (x << k) | (x >> (64 - k));
        }

        void siphash_mix(std::array<std::uint64_t, 4> &s, std::uint64_t a, std::uint64_t b)
        {
            s[0] += a;
            s[1] = rotl64(s[1], 13);
            s[1] ^= s[0];
            s[2] += b;
            s[3] = rotl64(s[3], 17);
            s[3] ^= s[2];
            s[0] = rotl64(s[0], 32);

            s[2] += s[1];
            s[1] = rotl64(s[1], 21);
            s[1] ^= s[2];
            s[0] += s[3];
            s[3] = rotl64(s[3], 16);
            s[3] ^= s[0];
            s[2] = rotl64(s[2], 32);
        }

        void mix_value(const auto &val)
        {
            lib::blake2s_update(_base.pool, std::as_bytes(std::span { &val, 1 }));
        }

        void mix_string(const char *str)
        {
            if (str == nullptr)
                return;

            const std::string_view sv { str };
            if (sv.empty())
                return;

            lib::blake2s_update(_base.pool, std::as_bytes(std::span { sv }));
        }

        void mix_hw_rng(std::size_t target)
        {
            std::array<std::byte, 64> buf;
            std::size_t mixed = 0;
            while (mixed < target)
            {
                const auto got = arch::hardware_random(buf);
                if (got == 0)
                    break;

                lib::blake2s_update(_base.pool, std::span { buf.data(), got });
                mixed += got;
                if (got < buf.size())
                    break;
            }
            memzero(buf.data(), buf.size());
        }

        void mix_jitter(std::size_t iters)
        {
            for (std::size_t i = 0; i < iters; i++)
            {
                const auto cycles = arch::cycle_count();
                const auto now = static_cast<std::uint64_t>(chrono::now(chrono::realtime).to_ns());
                lib::blake2s_update(_base.pool, std::as_bytes(std::span { &cycles, 1 }));
                lib::blake2s_update(_base.pool, std::as_bytes(std::span { &now, 1 }));
                arch::pause();
            }
        }

        void mix_boot_data()
        {
            const auto bt = boot::time();
            const auto hhdm = boot::get_hhdm_offset();
            mix_value(bt);
            mix_value(hhdm);

            {
                const auto kaddr = boot::requests::kernel_address.response;
                const auto pbase = kaddr->physical_base;
                const auto vbase = kaddr->virtual_base;
                mix_value(pbase);
                mix_value(vbase);
            }

            {
                const auto rsdp = boot::requests::rsdp.response;
                const auto addr = rsdp->address;
                mix_value(addr);
            }

            {
                const auto memmap = boot::requests::memmap.response;
                const auto cnt = memmap->entry_count;
                mix_value(cnt);

                for (std::uint64_t i = 0; i < cnt; i++)
                {
                    const auto entry = memmap->entries[i];
                    const auto base = entry->base;
                    const auto length = entry->length;
                    const auto type = entry->type;

                    mix_value(base);
                    mix_value(length);
                    mix_value(type);
                }
            }

            {
                const auto fb = boot::requests::framebuffer.response;
                const auto cnt = fb->framebuffer_count;
                mix_value(cnt);

                for (std::uint64_t i = 0; i < cnt; i++)
                {
                    const auto frm = fb->framebuffers[i];
                    const auto addr = reinterpret_cast<std::uintptr_t>(frm->address);
                    const auto width = frm->width;
                    const auto height = frm->height;
                    const auto pitch = frm->pitch;

                    mix_value(addr);
                    mix_value(width);
                    mix_value(height);
                    mix_value(pitch);
                }
            }

            if (const auto mods = boot::requests::module_.response)
            {
                const auto cnt = mods->module_count;
                mix_value(cnt);

                for (std::uint64_t i = 0; i < cnt; i++)
                {
                    const auto mod = mods->modules[i];
                    const auto addr = reinterpret_cast<std::uintptr_t>(mod->address);
                    const auto size = mod->size;

                    mix_value(addr);
                    mix_value(size);
                    mix_string(mod->path);
                    mix_string(mod->string);
                }
            }
        }

        void base_extract()
        {
            std::array<std::byte, lib::blake2s_hash_size> digest;
            lib::blake2s_state snapshot = _base.pool;
            lib::blake2s_final(snapshot, digest);

            for (std::size_t i = 0; i < lib::chacha20_key_size; i++)
                _base.key[i] ^= digest[i];

            lib::blake2s_init_key(_base.pool, lib::blake2s_hash_size, _base.key);
            _base.generation++;
            _base_generation.store(_base.generation, std::memory_order_release);

            memzero(digest.data(), digest.size());
            memzero(&snapshot, sizeof(snapshot));
        }

        void base_initialise()
        {
            lib::blake2s_init(_base.pool, lib::blake2s_hash_size);
            mix_jitter(32);
            mix_hw_rng(64);
            base_extract();

            _base.initialised = true;
            _jitter_armed.store(true, std::memory_order_release);
        }

        void drain_irq_pools()
        {
            for (std::size_t i = 0; i < cpu::count(); i++)
            {
                const auto base = cpu::local::nth_base(i);
                auto &pool = _irq_pool.unsafe_get(base);

                std::array<std::uint64_t, 4> snapshot;
                bool any = false;
                for (std::size_t j = 0; j < snapshot.size(); j++)
                {
                    snapshot[j] = __atomic_exchange_n(&pool.entries[j], 0, __ATOMIC_RELAXED);
                    any |= (snapshot[j] != 0);
                }

                if (any)
                    lib::blake2s_update(_base.pool, std::as_bytes(std::span { snapshot }));
            }
        }

        void periodic_reseed()
        {
            {
                const std::unique_lock _ { _base_lock };
                if (!_base.initialised)
                    base_initialise();

                mix_hw_rng(64);
                mix_jitter(32);
                drain_irq_pools();
                base_extract();
            }
            sched::schedule_work_after_ns(periodic_reseed, reseed_period_ns);
        }

        void derive_local(local_crng &local)
        {
            const std::unique_lock _ { _base_lock };
            if (!_base.initialised)
                base_initialise();

            drain_irq_pools();

            std::array<std::byte, lib::chacha20_block_size> block;
            lib::chacha20_block(_base.key, zero_nonce, 0, block);
            std::copy_n(block.begin(), lib::chacha20_key_size, _base.key.begin());
            std::copy_n(
                block.begin() + lib::chacha20_key_size, lib::chacha20_key_size, local.key.begin()
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

            constexpr std::size_t output_per_block =
                lib::chacha20_block_size - lib::chacha20_key_size;
            while (!out.empty())
            {
                std::array<std::byte, lib::chacha20_block_size> block;
                lib::chacha20_block(local.key, zero_nonce, local.counter, block);
                local.counter++;

                std::copy_n(block.begin(), lib::chacha20_key_size, local.key.begin());

                const std::size_t copylen = std::min(out.size(), output_per_block);
                std::copy_n(block.begin() + lib::chacha20_key_size, copylen, out.begin());
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
            base_initialise();
        lib::blake2s_update(_base.pool, data);
        base_extract();
    }

    void add_irq_jitter(std::size_t vector, std::uintptr_t ip)
    {
        if (!_jitter_armed.load(std::memory_order_acquire))
            return;

        auto &p = _irq_pool.unsafe_get();
        const auto cycles = arch::cycle_count();
        const auto data =
            (static_cast<std::uint64_t>(vector) << 32) ^ static_cast<std::uint64_t>(ip);
        siphash_mix(p.entries, cycles, data);
    }

    std::uint32_t get_u32()
    {
        lib::lock::acquire_irq();
        const auto ret = pop_batch(_batch_u32.unsafe_get());
        lib::lock::release_irq();
        return ret;
    }

    std::uint64_t get_u64()
    {
        lib::lock::acquire_irq();
        const auto ret = pop_batch(_batch_u64.unsafe_get());
        lib::lock::release_irq();
        return ret;
    }

    std::ssize_t get_bytes(lib::maybe_uspan<std::byte> buffer)
    {
        if (buffer.is_user())
        {
            lib::membuffer buf { std::min(buffer.size_bytes(), 1024uz) };
            std::size_t progress = 0;
            while (progress < buffer.size_bytes())
            {
                const auto chunk_size = std::min(buffer.size_bytes() - progress, buf.size_bytes());

                lib::lock::acquire_irq();
                fill_local(_local.unsafe_get(), std::span { buf.data(), chunk_size });
                lib::lock::release_irq();

                if (!buffer.subspan(progress, chunk_size).copy_from(buf.data()))
                    return -1;
                progress += chunk_size;
            }
            return static_cast<std::ssize_t>(progress);
        }

        lib::lock::acquire_irq();
        fill_local(_local.unsafe_get(), buffer.span());
        lib::lock::release_irq();
        return buffer.size_bytes();
    }

    lib::initgraph::task random_init_task {
        "random.initialise", lib::initgraph::presched_init_engine,
        lib::initgraph::require { arch::cpus_stage() }, [] {
            {
                const std::unique_lock _ { _base_lock };
                if (!_base.initialised)
                    base_initialise();

                mix_boot_data();
                mix_hw_rng(1024);
                mix_jitter(64);
                drain_irq_pools();
                base_extract();
            }
            sched::schedule_work_after_ns(periodic_reseed, reseed_period_ns);
        }
    };
} // namespace random
