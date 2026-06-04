// Copyright (C) 2024-2026  ilobilo

export module system.sched:futex;

import system.memory.virt;
import lib;
import std;

import :thread;
import :sleep;

export namespace sched::futex
{
    enum op : int
    {
        futex_wait = 0,
        futex_wake = 1,
        futex_fd = 2,
        futex_requeue = 3,
        futex_cmp_requeue = 4,
        futex_wake_op = 5,
        futex_lock_pi = 6,
        futex_unlock_pi = 7,
        futex_trylock_pi = 8,
        futex_wait_bitset = 9,
        futex_wake_bitset = 10,
        futex_wait_requeue_pi = 11,
        futex_cmp_requeue_pi = 12,
        futex_lock_pi2 = 13,
    };

    enum flag : int
    {
        futex_private_flag = 128,
        futex_clock_realtime = 256,
    };

    constexpr int futex_cmd_mask = ~(futex_private_flag | futex_clock_realtime);
    constexpr std::uint32_t bitset_match_any = 0xFFFFFFFFu;

    struct key_t
    {
        enum class type : std::uint8_t { private_, shared };

        type type;
        union {
            struct {
                vmm::vmspace *vmspace;
                std::uintptr_t vaddr;
            } pvt;
            struct {
                vmm::object *obj;
                std::uint64_t offset;
            } shr;
        };

        bool operator==(const key_t &rhs) const
        {
            if (type != rhs.type)
                return false;

            switch (type)
            {
                case type::private_:
                    return pvt.vmspace == rhs.pvt.vmspace && pvt.vaddr == rhs.pvt.vaddr;
                case type::shared:
                    return shr.obj == rhs.shr.obj && shr.offset == rhs.shr.offset;
            }
            return false;
        }
    };

    struct waiter_t
    {
        thread_t *thread;
        key_t key;
        std::uint32_t bitset;
        lib::intrusive_list_hook<waiter_t> hook;
        sleep_entry_t timeout;

        vmm::object::ptr obj_ref;
        std::atomic<lib::spinlock_irq *> lock_ptr;
    };

    struct robust_list_t
    {
        robust_list_t __user *next;
    };

    struct robust_list_head_t
    {
        robust_list_t list;
        long futex_offset;
        robust_list_t __user *list_op_pending;
    };

    enum robust_bits : std::uint32_t
    {
        futex_tid_mask = 0x3FFFFFFFu,
        futex_owner_died = 0x40000000u,
        futex_waiters = 0x80000000u
    };
    constexpr std::size_t robust_list_walk_max = 2048;

    lib::expect<key_t> resolve(std::uint32_t __user *uaddr, bool private_);

    lib::expect<void> wait(
        std::uint32_t __user *uaddr, const key_t &key,
        std::uint32_t val, std::uint32_t bitset,
        std::optional<std::uint64_t> wait_ns
    );

    std::uint32_t wake(const key_t &key, std::int32_t num, std::uint32_t bitset);

    lib::expect<std::int32_t> wake_op(
        const key_t &key1, const key_t &key2, std::uint32_t __user *uaddr2,
        std::int32_t nr_wake, std::int32_t nr_wake2,
        std::uint32_t op_encoding
    );

    lib::expect<std::pair<std::uint32_t, std::uint32_t>> requeue(
        const key_t &key1, const key_t &key2, std::uint32_t __user *uaddr_cmp,
        std::int32_t nr_wake, std::int32_t nr_requeue,
        std::optional<std::uint32_t> cmpval
    );

    void cleanup_robust_list(thread_t *thread);
} // export namespace sched::futex
