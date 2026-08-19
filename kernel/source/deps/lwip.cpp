// Copyright (C) 2024-2026  ilobilo

module;

#include <lwip/sys.h>
#include <lwip/arch.h>
#include <lwip/tcpip.h>
#include <lwip/netif.h>
#include <lwip/ethip6.h>
#include <lwip/netifapi.h>
#include <netif/etharp.h>

module lwip;

import system.sched.wait_queue;
import system.cpu.local;
import system.chrono;
import system.random;
import system.sched;
import arch;
import std;

extern "C"
{
    struct lwip_port_sem_t
    {
        std::atomic<int> count;
        sched::wait_queue_t waiters;
    };

    struct lwip_port_mutex_t
    {
        sched::mutex_t inner;
    };

    struct lwip_port_mbox_t
    {
        void **slots;
        int capacity;
        int head;
        int tail;
        lib::spinlock_irq lock;
        lwip_port_sem_t free_slots;
        lwip_port_sem_t used_slots;
    };

    struct lwip_port_thread_t
    {
        std::shared_ptr<sched::thread_t> handle;
    };
} // extern "C"

namespace lwip
{
    namespace
    {
        u32_t sem_wait(lwip_port_sem_t *sem, u32_t timeout_ms)
        {
            const auto start = chrono::now(chrono::monotonic).to_ns();
            const auto deadline = start + static_cast<std::uint64_t>(timeout_ms) * 1'000'000ul;

            while (true)
            {
                const auto gen = sem->waiters.snapshot_gen();

                auto cur = sem->count.load(std::memory_order_acquire);
                while (cur > 0 && !sem->count.compare_exchange_weak(
                    cur, cur - 1, std::memory_order_acquire, std::memory_order_relaxed));

                const auto now = chrono::now(chrono::monotonic).to_ns();
                if (cur > 0)
                {
                    const std::uint64_t elapsed = (now - start) / 1'000'000;
                    return std::min(elapsed, SYS_ARCH_TIMEOUT - 1);
                }

                if (timeout_ms != 0)
                {
                    if (now >= deadline)
                        return SYS_ARCH_TIMEOUT;
                    sem->waiters.wait_unkillable_prepared(gen, deadline - now);
                }
                else sem->waiters.wait_unkillable_prepared(gen);
            }
        }

        void sem_signal(lwip_port_sem_t *sem)
        {
            sem->count.fetch_add(1, std::memory_order_release);
            sem->waiters.wake_one();
        }

        bool sem_try(lwip_port_sem_t *sem)
        {
            auto cur = sem->count.load(std::memory_order_acquire);
            while (cur > 0 && !sem->count.compare_exchange_weak(
                cur, cur - 1, std::memory_order_acquire, std::memory_order_relaxed));
            return cur > 0;
        }

        lib::spinlock prot_lock;
        cpu_local(std::size_t, prot_depth, 0uz);

        struct state_t
        {
            std::weak_ptr<dev::net::nic_t> nic;
            lib::membuffer buffer;
        };

        bool is_loopback(const dev::net::nic_t &nic)
        {
            return nic.type() == dev::net::arphrd::loopback;
        }

        err_t link_out(netif *nif, pbuf *buf)
        {
            auto state = static_cast<state_t *>(nif->state);
            auto nic = state->nic.lock();
            if (!nic)
                return ERR_ARG;

            if (buf->next == nullptr)
            {
                const std::span span { static_cast<const std::byte *>(buf->payload), buf->len };
                if (const auto ret = nic->transmit(span); !ret)
                    return to_err(ret.error());
                return ERR_OK;
            }

            const std::size_t needed = nic->mtu() + SIZEOF_ETH_HDR;
            if (buf->tot_len > needed)
                return ERR_BUF;

            if (state->buffer.size() < needed)
                state->buffer = lib::membuffer { needed };

            pbuf_copy_partial(buf, state->buffer.data(), buf->tot_len, 0);
            const auto span = state->buffer.span().subspan(0, buf->tot_len);
            if (const auto ret = nic->transmit(span); !ret)
                return to_err(ret.error());
            return ERR_OK;
        }

        err_t loop_out4(netif *nif, pbuf *buf, const ip4_addr_t *addr)
        {
            lib::unused(addr);
            return netif_loop_output(nif, buf);
        }

        // err_t loop_out6(netif *nif, pbuf *buf, const ip6_addr_t *addr)
        // {
        //     lib::unused(addr);
        //     return netif_loop_output(nif, buf);
        // }

        err_t netif_init(netif *nif)
        {
            auto nic = static_cast<state_t *>(nif->state)->nic.lock();
            if (!nic)
                return ERR_ARG;

            nif->mtu = nic->mtu();

            if (is_loopback(*nic))
            {
                nif->name[0] = 'l';
                nif->name[1] = 'o';

                nif->output = loop_out4;
                // nif->output_ip6 = loop_out6;
                nif->flags = NETIF_FLAG_IGMP;

                // IP_ADDR6_HOST(nif->ip6_addr, 0, 0, 0, 1);
                // nif->ip6_addr_state[0] = IP6_ADDR_VALID;
                return ERR_OK;
            }

            nif->name[0] = 'e';
            nif->name[1] = 'n';

            nif->output = etharp_output;
            nif->output_ip6 = ethip6_output;
            nif->linkoutput = link_out;

            nif->hwaddr_len = ETH_HWADDR_LEN;
            std::memcpy(nif->hwaddr, nic->mac().data(), ETH_HWADDR_LEN);
            nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

            // TODO: igmp_mac_filter and mld_mac_filter
            return ERR_OK;
        }

//         void setup_ip6(netif *nif)
//         {
//             auto nic = static_cast<state_t *>(nif->state)->nic.lock();
//             if (!nic || is_loopback(*nic))
//                 return;

//             netif_create_ip6_linklocal_address(nif, 1);
// #if LWIP_IPV6_AUTOCONFIG
//             nif->ip6_autoconfig_enabled = 1;
// #endif
//         }
    } // namespace

    lib::expect<void> attach(const std::shared_ptr<dev::net::nic_t> &nic)
    {
        auto nif = std::make_shared<netif>();
        auto state = std::make_unique<state_t>(nic);

        ip4_addr_t addr { }, mask { }, gw { };
        if (is_loopback(*nic))
        {
            IP4_ADDR(&addr, 127, 0, 0, 1);
            IP4_ADDR(&mask, 255, 0, 0, 0);
        }

        const auto ret = netifapi_netif_add(
            nif.get(), &addr, &mask, &gw,
            state.get(), netif_init, tcpip_input
        );
        if (const auto res = check_err(ret); !res)
            return res;

        // netifapi_netif_common(nif.get(), setup_ip6, nullptr);

        lib::unused(state.release());
        nic->lwip = std::move(nif);
        return { };
    }

    void deattach(const std::shared_ptr<dev::net::nic_t> &nic)
    {
        auto nif = static_cast<netif *>(nic->lwip.get());
        if (nif == nullptr)
            return;

        netifapi_netif_remove(nif);

        delete static_cast<state_t *>(std::exchange(nif->state, nullptr));
        nic->lwip.reset();
    }

    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "lwip.initialised",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task lwip_task
    {
        "lwip.init",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::entail { initialised_stage() },
        [] {
            sys_sem_t init_done;
            sys_sem_new(&init_done, 0);
            tcpip_init([](void *arg) {
                sys_sem_signal(static_cast<sys_sem_t *>(arg));
            }, &init_done);
            sys_arch_sem_wait(&init_done, 0);
            sys_sem_free(&init_done);
        }
    };
} // namespace lwip

extern "C"
{
    __attribute__((format(printf, 1, 2)))
    void lwip_port_diag(const char *fmt, ...)
    {
        // TODO: format
        lib::debug("lwip: {}", fmt);
    }

    void lwip_port_assert(const char *msg, const char *file, int line)
    {
        lib::panic("lwip: assertion failed: {} at {}:{}", msg, file, line);
        std::unreachable();
    }

    unsigned int lwip_port_rand()
    {
        return random::get_u32();
    }

    int *lwip_port_errno(void)
    {
        return &sched::current_thread()->err;
    }

    void sys_init()
    {
        lib::info("lwip: sys_init()");
    }

    u32_t sys_now()
    {
        return chrono::now(chrono::monotonic).to_ms();
    }

    sys_prot_t sys_arch_protect()
    {
        const auto ints = arch::int_switch_status(false);
        if (lwip::prot_depth.unsafe_get()++ == 0)
            lwip::prot_lock.lock();
        return ints;
    }

    void sys_arch_unprotect(sys_prot_t pval)
    {
        auto &depth = lwip::prot_depth.unsafe_get();
        lib::bug_on(depth == 0);
        if (--depth == 0)
            lwip::prot_lock.unlock();
        arch::int_switch(pval);
    }

    void sys_msleep(u32_t ms)
    {
        sched::sleep_for_ns(static_cast<std::uint64_t>(ms) * 1'000'000);
    }

    err_t sys_sem_new(sys_sem_t *sem, u8_t count)
    {
        (*sem = new lwip_port_sem_t)->count.store(count, std::memory_order_relaxed);
        return ERR_OK;
    }

    void sys_sem_signal(sys_sem_t *sem)
    {
        lwip::sem_signal(*sem);
    }

    u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
    {
        return lwip::sem_wait(*sem, timeout);
    }

    void sys_sem_free(sys_sem_t *sem)
    {
        delete *sem;
    }

    err_t sys_mutex_new(sys_mutex_t *mutex)
    {
        *mutex = new lwip_port_mutex_t;
        return ERR_OK;
    }

    void sys_mutex_lock(sys_mutex_t *mutex)
    {
        (*mutex)->inner.lock();
    }

    void sys_mutex_unlock(sys_mutex_t *mutex)
    {
        (*mutex)->inner.unlock();
    }

    void sys_mutex_free(sys_mutex_t *mutex)
    {
        delete *mutex;
    }

    err_t sys_mbox_new(sys_mbox_t *mbox, int size)
    {
        if (size <= 0)
            return ERR_ARG;

        *mbox = new lwip_port_mbox_t {
            .slots = new void *[size],
            .capacity = size,
            .head = 0,
            .tail = 0,
            .lock = { },
            .free_slots = { },
            .used_slots = { }
        };
        (*mbox)->free_slots.count.store(size, std::memory_order_relaxed);
        (*mbox)->used_slots.count.store(0, std::memory_order_relaxed);
        return ERR_OK;
    }

    void sys_mbox_post(sys_mbox_t *mbox, void *msg)
    {
        lwip::sem_wait(&(*mbox)->free_slots, 0);
        {
            const std::unique_lock _ { (*mbox)->lock };
            (*mbox)->slots[(*mbox)->tail] = msg;
            (*mbox)->tail = ((*mbox)->tail + 1) % (*mbox)->capacity;
        }
        lwip::sem_signal(&(*mbox)->used_slots);
    }

    err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
    {
        if (!lwip::sem_try(&(*mbox)->free_slots))
            return ERR_MEM;
        {
            const std::unique_lock _ { (*mbox)->lock };
            (*mbox)->slots[(*mbox)->tail] = msg;
            (*mbox)->tail = ((*mbox)->tail + 1) % (*mbox)->capacity;
        }
        lwip::sem_signal(&(*mbox)->used_slots);
        return ERR_OK;
    }

    err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
    {
        return sys_mbox_trypost(mbox, msg);
    }

    u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
    {
        const auto waited = lwip::sem_wait(&(*mbox)->used_slots, timeout);
        if (waited == SYS_ARCH_TIMEOUT)
            return SYS_ARCH_TIMEOUT;
        {
            const std::unique_lock _ { (*mbox)->lock };
            *msg = (*mbox)->slots[(*mbox)->head];
            (*mbox)->head = ((*mbox)->head + 1) % (*mbox)->capacity;
        }
        lwip::sem_signal(&(*mbox)->free_slots);
        return waited;
    }

    u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
    {
        if (!lwip::sem_try(&(*mbox)->used_slots))
            return SYS_MBOX_EMPTY;
        {
            const std::unique_lock _ { (*mbox)->lock };
            *msg = (*mbox)->slots[(*mbox)->head];
            (*mbox)->head = ((*mbox)->head + 1) % (*mbox)->capacity;
        }
        lwip::sem_signal(&(*mbox)->free_slots);
        return 0;
    }

    void sys_mbox_free(sys_mbox_t *mbox)
    {
        if ((*mbox)->used_slots.count.load(std::memory_order_acquire) != 0)
            lib::warn("lwip: freeing mailbox that still has messages");

        delete[] (*mbox)->slots;
        delete *mbox;
    }

    sys_thread_t sys_thread_new(
        const char *name, void (*thread)(void *arg),
        void *arg, int stacksize, int prio
    )
    {
        lib::unused(stacksize, prio);
        auto handle = sched::spawn(thread, arg);
        if (handle && name != nullptr)
            handle->comm = name;
        return new lwip_port_thread_t { std::move(handle) };
    }
} // extern "C"
