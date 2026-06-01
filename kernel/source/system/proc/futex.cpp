// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.memory.virt;

namespace sched::futex
{
    namespace
    {
        struct bucket_t
        {
            lib::spinlock_irq lock;
            lib::intrusive_list<
                waiter_t, &waiter_t::hook
            > waiters;
        };

        constexpr std::size_t nbuckets = 1024;
        std::array<bucket_t, nbuckets> buckets;

        std::uint64_t hash_key(const key_t &key)
        {
            switch (key.type)
            {
                case key_t::type::private_:
                    return lib::hash::fnv1a(&key.pvt, sizeof(key.pvt)) ^ 0x1;
                case key_t::type::shared:
                    return lib::hash::fnv1a(&key.shr, sizeof(key.shr)) ^ 0x2;
            }
            __builtin_unreachable();
        }

        bucket_t &bucket_for(const key_t &key)
        {
            return buckets[hash_key(key) & (nbuckets - 1)];
        }

        bool valid_uaddr(std::uint32_t __user *uaddr)
        {
            const auto raw = reinterpret_cast<std::uintptr_t>(
                lib::remove_user_cast<std::uint32_t>(uaddr)
            );
            return uaddr != nullptr && (raw & 0x3) == 0;
        }

        std::int32_t sign_extend(std::uint32_t val)
        {
            return static_cast<std::int32_t>(val << 20) >> 20;
        }

        enum wake_op_kind
        {
            wake_op_set = 0,
            wake_op_add = 1,
            wake_op_or = 2,
            wake_op_andn = 3,
            wake_op_xor = 4,
            wake_op_oparg_shift_bit = 0x8
        };

        enum wake_op_cmp
        {
            wake_op_cmp_eq = 0,
            wake_op_cmp_ne = 1,
            wake_op_cmp_lt = 2,
            wake_op_cmp_le = 3,
            wake_op_cmp_gt = 4,
            wake_op_cmp_ge = 5,
        };

        std::optional<std::uint32_t> apply_wake_op(int op, std::uint32_t old, std::int32_t oparg)
        {
            const auto arg = static_cast<std::uint32_t>(oparg);
            switch (op)
            {
                case wake_op_set:
                    return arg;
                case wake_op_add:
                    return old + arg;
                case wake_op_or:
                    return old | arg;
                case wake_op_andn:
                    return old & ~arg;
                case wake_op_xor:
                    return old ^ arg;
                default:
                    return std::nullopt;
            }
        }

        std::optional<bool> apply_wake_cmp(int cmp, std::uint32_t old, std::int32_t cmparg)
        {
            const auto arg = static_cast<std::uint32_t>(cmparg);
            switch (cmp)
            {
                case wake_op_cmp_eq:
                    return old == arg;
                case wake_op_cmp_ne:
                    return old != arg;
                case wake_op_cmp_lt:
                    return old <  arg;
                case wake_op_cmp_le:
                    return old <= arg;
                case wake_op_cmp_gt:
                    return old >  arg;
                case wake_op_cmp_ge:
                    return old >= arg;
                default:
                    return std::nullopt;
            }
        }

        bool handle_robust_entry(robust_list_t __user *entry, long futex_offset, pid_t tid)
        {
            const auto entry_addr = reinterpret_cast<std::uintptr_t>(entry);
            auto uaddr = reinterpret_cast<std::uint32_t __user *>(entry_addr + futex_offset);
            if (!valid_uaddr(uaddr))
                return true;

            std::uint32_t cur;
            if (!lib::copy_from_user(&cur, uaddr, sizeof(cur)))
                return false;

            while (true)
            {
                if ((cur & futex_tid_mask) != static_cast<std::uint32_t>(tid))
                    return true;

                const auto next = (cur & futex_waiters) | futex_owner_died;
                std::uint32_t expected = cur;
                if (!lib::cmpxchg_user(uaddr, expected, next))
                    return false;

                if (expected == cur)
                {
                    if (cur & futex_waiters)
                    {
                        if (auto key = resolve(uaddr, false))
                            wake(*key, 1, bitset_match_any);
                    }
                    return true;
                }
                cur = expected;
            }
        }
    } // namespace

    lib::expect<key_t> resolve(std::uint32_t __user *uaddr, bool private_)
    {
        if (!valid_uaddr(uaddr))
            return std::unexpected { lib::err::invalid_address };

        auto proc = current_process();
        const auto vaddr = reinterpret_cast<std::uintptr_t>(uaddr);

        if (private_)
        {
            return key_t {
                .type = key_t::type::private_,
                .pvt = {
                    .vmspace = proc->vmspace.get(),
                    .vaddr = vaddr,
                }
            };
        }

        const auto psize = vmm::default_page_size();
        const auto npsize = vmm::pagemap::from_page_size(psize);
        const auto pgidx = vaddr / npsize;
        const auto in_page = vaddr & (npsize - 1);

        auto locked = proc->vmspace->tree.lock();
        const auto overlap = locked->overlapping(pgidx, pgidx + 1);
        if (overlap.empty())
            return std::unexpected { lib::err::invalid_address };

        auto &entry = overlap.front();
        if (!entry.obj)
        {
            return key_t {
                .type = key_t::type::private_,
                .pvt = {
                    .vmspace = proc->vmspace.get(),
                    .vaddr = vaddr,
                }
            };
        }

        return key_t {
            .type = key_t::type::shared,
            .shr = {
                .obj = entry.obj.get(),
                .offset = (entry.offp + (pgidx - entry.startp)) * npsize + in_page,
            }
        };
    }

    lib::expect<void> wait(
        std::uint32_t __user *uaddr, const key_t &key,
        std::uint32_t val, std::uint32_t bitset,
        std::optional<std::uint64_t> wait_ns
    )
    {
        if (bitset == 0)
            return std::unexpected { lib::err::invalid_argument };
        if (!valid_uaddr(uaddr))
            return std::unexpected { lib::err::invalid_address };

        auto thread = current_thread();
        if (thread->has_flag(thread_flags::kill_pending))
            return std::unexpected { lib::err::interrupted };

        waiter_t waiter {
            .thread = thread,
            .key = key,
            .bitset = bitset,
            .hook = { },
            .timeout = { },
            .obj_ref = (key.type == key_t::type::shared)
                ? vmm::object::ptr { key.shr.obj }
                : vmm::object::ptr { },
            .lock_ptr = nullptr
        };
        waiter.timeout.thread = thread;

        auto &bucket = bucket_for(key);
        bucket.lock.lock();

        std::uint32_t cur;
        if (!lib::copy_from_user(&cur, uaddr, sizeof(cur)))
        {
            bucket.lock.unlock();
            return std::unexpected { lib::err::invalid_address };
        }

        if (cur != val)
        {
            bucket.lock.unlock();
            return std::unexpected { lib::err::try_again };
        }

        bucket.waiters.push_back(&waiter);
        waiter.lock_ptr.store(&bucket.lock, std::memory_order_release);
        thread->state.store(thread_state::sleeping, std::memory_order_seq_cst);

        if (thread->has_flag(thread_flags::kill_pending) ||
            thread->has_flag(thread_flags::interrupted) ||
            signal_pending_for(thread))
        {
            auto expected = thread_state::sleeping;
            if (thread->state.compare_exchange_strong(
                expected, thread_state::running,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            {
                bucket.waiters.remove(&waiter);
                waiter.lock_ptr.store(nullptr, std::memory_order_release);
                bucket.lock.unlock();
                thread->test_and_clear_flag(thread_flags::interrupted);
                return std::unexpected { lib::err::interrupted };
            }
        }

        if (wait_ns.has_value())
            arm_thread_timeout(&waiter.timeout, *wait_ns);

        bucket.lock.unlock();
        schedule();

        const bool interrupted = thread->test_and_clear_flag(thread_flags::interrupted);
        const bool killed = thread->has_flag(thread_flags::kill_pending);

        if (wait_ns.has_value() && !waiter.timeout.expired)
            cancel_thread_timeout(&waiter.timeout);

        while (true)
        {
            auto locked = waiter.lock_ptr.load(std::memory_order_acquire);
            if (locked == nullptr)
                break;

            locked->lock();
            if (waiter.lock_ptr.load(std::memory_order_acquire) != locked)
            {
                locked->unlock();
                continue;
            }

            auto &current = bucket_for(waiter.key);
            if (current.waiters.find(&waiter) != current.waiters.end())
                current.waiters.remove(&waiter);
            waiter.lock_ptr.store(nullptr, std::memory_order_release);
            locked->unlock();
            break;
        }

        if (interrupted || killed)
            return std::unexpected { lib::err::interrupted };
        if (wait_ns.has_value() && waiter.timeout.expired)
            return std::unexpected { lib::err::timed_out };
        return { };
    }

    std::uint32_t wake(const key_t &key, std::int32_t num, std::uint32_t bitset)
    {
        if (num <= 0 || bitset == 0)
            return 0;

        auto &bucket = bucket_for(key);

        lib::intrusive_list<waiter_t, &waiter_t::hook> targets;
        std::int32_t collected = 0;
        {
            const std::unique_lock _ { bucket.lock };
            for (auto it = bucket.waiters.begin(); it != bucket.waiters.end() && collected < num; )
            {
                auto waiter = (it++).value();

                if (waiter->key != key || !(waiter->bitset & bitset))
                    continue;

                bucket.waiters.remove(waiter);
                waiter->lock_ptr.store(nullptr, std::memory_order_release);
                targets.push_back(waiter);
                collected++;
            }
        }

        std::uint32_t woken = 0;
        while (!targets.empty())
        {
            auto waiter = targets.pop_front();
            if (wake_up(waiter->thread, true))
                woken++;
        }
        return woken;
    }

    lib::expect<std::int32_t> wake_op(
        const key_t &key1, const key_t &key2, std::uint32_t __user *uaddr2,
        std::int32_t nr_wake, std::int32_t nr_wake2,
        std::uint32_t op_encoding
    )
    {
        if (!valid_uaddr(uaddr2))
            return std::unexpected { lib::err::invalid_address };

        const auto op_field = (op_encoding >> 28) & 0xF;
        const auto cmp = static_cast<int>((op_encoding >> 24) & 0xF);
        const int op = op_field & 0x7;
        const bool oparg_shift = (op_field & wake_op_oparg_shift_bit) != 0;

        auto oparg = sign_extend((op_encoding >> 12) & 0xFFF);
        const auto cmparg = sign_extend(op_encoding & 0xFFF);

        if (oparg_shift)
        {
            if (oparg < 0 || oparg > 31)
                return std::unexpected { lib::err::invalid_argument };
            oparg = 1u << oparg;
        }

        std::uint32_t old;
        if (!lib::copy_from_user(&old, uaddr2, sizeof(old)))
            return std::unexpected { lib::err::invalid_address };

        while (true)
        {
            const auto new_val = apply_wake_op(op, old, oparg);
            if (!new_val)
                return std::unexpected { lib::err::invalid_argument };

            std::uint32_t expected = old;
            if (!lib::cmpxchg_user(uaddr2, expected, *new_val))
                return std::unexpected { lib::err::invalid_address };

            if (expected == old)
                break;
            old = expected;
        }

        const auto cond = apply_wake_cmp(cmp, old, cmparg);
        if (!cond)
            return std::unexpected { lib::err::invalid_argument };

        auto woken = static_cast<std::int32_t>(wake(key1, nr_wake, bitset_match_any));
        if (*cond)
            woken += static_cast<std::int32_t>(wake(key2, nr_wake2, bitset_match_any));
        return woken;
    }

    lib::expect<std::pair<std::uint32_t, std::uint32_t>> requeue(
        const key_t &key1, const key_t &key2, std::uint32_t __user *uaddr_cmp,
        std::int32_t nr_wake, std::int32_t nr_requeue,
        std::optional<std::uint32_t> cmpval
    )
    {
        if (nr_wake < 0 || nr_requeue < 0)
            return std::unexpected { lib::err::invalid_argument };

        auto &b1 = bucket_for(key1);
        auto &b2 = bucket_for(key2);

        auto first = (&b1 <= &b2) ? &b1 : &b2;
        auto second = (&b1 < &b2) ? &b2 : (&b1 > &b2 ? &b1 : nullptr);

        first->lock.lock();
        if (second)
            second->lock.lock();

        const auto unlock_buckets = [&] {
            if (second)
                second->lock.unlock();
            first->lock.unlock();
        };

        if (cmpval.has_value())
        {
            std::uint32_t cur;
            if (!lib::copy_from_user(&cur, uaddr_cmp, sizeof(cur)))
            {
                unlock_buckets();
                return std::unexpected { lib::err::invalid_address };
            }

            if (cur != *cmpval)
            {
                unlock_buckets();
                return std::unexpected { lib::err::try_again };
            }
        }

        lib::intrusive_list<waiter_t, &waiter_t::hook> to_wake;
        std::int32_t collected_wake = 0;
        for (auto it = b1.waiters.begin(); it != b1.waiters.end() && collected_wake < nr_wake; )
        {
            auto waiter = (it++).value();
            if (waiter->key != key1)
                continue;

            b1.waiters.remove(waiter);
            waiter->lock_ptr.store(nullptr, std::memory_order_release);
            to_wake.push_back(waiter);
            collected_wake++;
        }

        std::int32_t requeued = 0;
        for (auto it = b1.waiters.begin(); it != b1.waiters.end() && requeued < nr_requeue; )
        {
            auto waiter = (it++).value();
            if (waiter->key != key1)
                continue;

            b1.waiters.remove(waiter);
            waiter->key = key2;
            waiter->obj_ref = (key2.type == key_t::type::shared)
                ? vmm::object::ptr { key2.shr.obj }
                : vmm::object::ptr { };
            waiter->lock_ptr.store(&b2.lock, std::memory_order_release);
            b2.waiters.push_back(waiter);
            requeued++;
        }

        unlock_buckets();

        std::uint32_t woken = 0;
        while (!to_wake.empty())
        {
            auto waiter = to_wake.pop_front();
            if (wake_up(waiter->thread, true))
                woken++;
        }

        return std::make_pair(woken, requeued);
    }

    void cleanup_robust_list(thread_t *thread)
    {
        if (thread->robust_list == 0)
            return;

        robust_list_head_t head;
        const auto uhead = reinterpret_cast<robust_list_head_t __user *>(thread->robust_list);
        if (!lib::copy_from_user(&head, uhead, sizeof(head)))
            return;

        if (head.list_op_pending != nullptr)
            handle_robust_entry(head.list_op_pending, head.futex_offset, thread->tid);

        const auto sentinel = reinterpret_cast<robust_list_t __user *>(uhead);
        auto current = head.list.next;
        std::size_t visited = 0;

        while (current != sentinel && visited < robust_list_walk_max)
        {
            if (current != head.list_op_pending)
            {
                if (!handle_robust_entry(current, head.futex_offset, thread->tid))
                    break;
            }

            robust_list_t entry;
            if (!lib::copy_from_user(&entry, current, sizeof(entry)))
                break;

            current = entry.next;
            visited++;
        }
    }
} // namespace sched::futex
