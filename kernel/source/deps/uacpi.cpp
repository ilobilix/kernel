// Copyright (C) 2024-2025  ilobilo

#include <uacpi/kernel_api.h>

import ilobilix;
import magic_enum;
import lib;
import std;

namespace uacpi
{
    namespace
    {
        using queue_type = lib::locker<
            std::deque<
                std::pair<uacpi_work_handler, uacpi_handle>
            >, lib::rwspinlock
        >;
        queue_type notify { };
        queue_type gpe { };

        lib::semaphore added { };
        lib::semaphore completed { };

        bool is_empty()
        {
            const auto nlocked = notify.read_lock();
            const auto glocked = gpe.read_lock();
            return nlocked->empty() && glocked->empty();
        }
    } // namespace

    void worker_caller(queue_type &queue)
    {
        while (true)
        {
            added.wait();
            bool worked = false;
            {
                auto locked = queue.write_lock();
                while (!locked->empty())
                {
                    auto [handler, handle] = locked->pop_front_element();
                    handler(handle);
                    worked = true;
                }
            }
            if (worked)
                completed.signal(true);
        }
    }

    void notify_worker()
    {
        worker_caller(notify);
        arch::halt(true);
    }

    void gpe_worker()
    {
        worker_caller(gpe);
        arch::halt(true);
    }

    lib::initgraph::task uacpi_task
    {
        "acpi.create-workers",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { sched::pid0_initialised_stage(), acpi::initialised_stage() },
        lib::initgraph::entail { acpi::workers_stage() },
        [] {
            sched::spawn(0, reinterpret_cast<std::uintptr_t>(notify_worker));
            sched::spawn(0, reinterpret_cast<std::uintptr_t>(gpe_worker));
        }
    };
} // namespace uacpi

extern "C"
{
#ifdef UACPI_KERNEL_INITIALIZATION
    uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl)
    {
        lib::debug("uacpi_kernel_initialize({})", static_cast<int>(current_init_lvl));
        switch (current_init_lvl)
        {
            case UACPI_INIT_LEVEL_EARLY:
                break;
            case UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED:
                break;
            case UACPI_INIT_LEVEL_NAMESPACE_LOADED:
                break;
            case UACPI_INIT_LEVEL_NAMESPACE_INITIALIZED:
                break;
        }
        return UACPI_STATUS_OK;
    }

    void uacpi_kernel_deinitialize()
    {
        lib::debug("uacpi_kernel_deinitialize()");
    }
#endif

    uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rdsp_address)
    {
        *out_rdsp_address = lib::fromhh(reinterpret_cast<uacpi_phys_addr>(acpi::get_rsdp()));
        return UACPI_STATUS_OK;
    }

    struct pci_dev
    {
        std::shared_ptr<pci::configio> io;
        uacpi_pci_address addr;
    };

    uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle)
    {
        auto io = pci::getio(address.segment, address.bus);
        if (!io) [[unlikely]]
            return UACPI_STATUS_INVALID_ARGUMENT;

        *out_handle = new pci_dev { io, address };
        return UACPI_STATUS_OK;
    }

    void uacpi_kernel_pci_device_close(uacpi_handle handle)
    {
        delete reinterpret_cast<pci_dev *>(handle);
    }

    extern "C++" template<typename Type>
    uacpi_status uacpi_kernel_pci_read(uacpi_handle device, uacpi_size offset, Type *value)
    {
        auto dev = reinterpret_cast<pci_dev *>(device);
        *value = dev->io->read<Type>(dev->addr.segment, dev->addr.bus, dev->addr.device, dev->addr.function, offset);
        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8 *value)
    {
        return uacpi_kernel_pci_read<std::uint8_t>(device, offset, value);
    }

    uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16 *value)
    {
        return uacpi_kernel_pci_read<std::uint16_t>(device, offset, value);
    }

    uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32 *value)
    {
        return uacpi_kernel_pci_read<std::uint32_t>(device, offset, value);
    }

    extern "C++" template<typename Type>
    uacpi_status uacpi_kernel_pci_write(uacpi_handle device, uacpi_size offset, Type value)
    {
        auto dev = reinterpret_cast<pci_dev *>(device);
        dev->io->write<Type>(dev->addr.segment, dev->addr.bus, dev->addr.device, dev->addr.function, offset, value);
        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value)
    {
        return uacpi_kernel_pci_write<std::uint8_t>(device, offset, value);
    }

    uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value)
    {
        return uacpi_kernel_pci_write<std::uint16_t>(device, offset, value);
    }

    uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value)
    {
        return uacpi_kernel_pci_write<std::uint32_t>(device, offset, value);
    }

    uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size, uacpi_handle *out_handle)
    {
        *out_handle = reinterpret_cast<uacpi_handle>(base);
        return UACPI_STATUS_OK;
    }

    void uacpi_kernel_io_unmap(uacpi_handle) { }

    extern "C++" template<typename Type>
    uacpi_status uacpi_kernel_io_read(uacpi_handle handle, uacpi_size offset, Type *value)
    {
        if constexpr (lib::io::supported)
        {
            auto address = reinterpret_cast<std::size_t>(handle) + offset;
            *value = lib::io::in<Type>(address);
            return UACPI_STATUS_OK;
        }
        return UACPI_STATUS_UNIMPLEMENTED;
    }

    uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *value)
    {
        return uacpi_kernel_io_read<std::uint8_t>(handle, offset, value);
    }

    uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *value)
    {
        return uacpi_kernel_io_read<std::uint16_t>(handle, offset, value);
    }

    uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *value)
    {
        return uacpi_kernel_io_read<std::uint32_t>(handle, offset, value);
    }

    extern "C++" template<typename Type>
    uacpi_status uacpi_kernel_io_write(uacpi_handle handle, uacpi_size offset, Type value)
    {
        if constexpr (lib::io::supported)
        {
            auto address = reinterpret_cast<std::size_t>(handle) + offset;
            lib::io::out<Type>(address, value);
            return UACPI_STATUS_OK;
        }
        return UACPI_STATUS_UNIMPLEMENTED;
    }

    uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 value)
    {
        return uacpi_kernel_io_write<std::uint8_t>(handle, offset, value);
    }

    uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 value)
    {
        return uacpi_kernel_io_write<std::uint16_t>(handle, offset, value);
    }

    uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value)
    {
        return uacpi_kernel_io_write<std::uint32_t>(handle, offset, value);
    }

    void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
    {
        auto &pmap = vmm::kernel_pagemap;

        // const auto psize = vmm::pagemap::max_page_size(len);
        const auto psize = vmm::page_size::small;
        const auto npsize = vmm::pagemap::from_page_size(psize);

        const auto paddr = lib::align_down(addr, npsize);
        const auto size = lib::align_up((addr - paddr) + len, npsize);

        const auto vaddr = vmm::alloc_vspace(lib::div_roundup(size, pmm::page_size));

        if (const auto ret = pmap->map(vaddr, paddr, size, vmm::pflag::rwg, psize); !ret)
            lib::panic("could not map uacpi memory: {}", magic_enum::enum_name(ret.error()));

        return reinterpret_cast<std::uint8_t *>(vaddr) + (addr - paddr);
    }

    void uacpi_kernel_unmap(void *addr, uacpi_size len)
    {
#if !ILOBILIX_MAX_UACPI_POINTS
        auto &pmap = vmm::kernel_pagemap;

        const auto psize = vmm::page_size::small;
        const auto npsize = vmm::pagemap::from_page_size(psize);

        const auto paddr = reinterpret_cast<std::uintptr_t>(addr);
        const auto vaddr = lib::align_down(paddr, npsize);
        const auto size = lib::align_up((paddr - vaddr) + len, npsize);

        if (!pmap->unmap(vaddr, size, psize))
            lib::panic("could not unmap acpi memory");
#else
        lib::unused(addr, len);
#endif
    }

    void *uacpi_kernel_alloc(uacpi_size size) { return lib::alloc(size); }

#ifdef UACPI_NATIVE_ALLOC_ZEROED
    void *uacpi_kernel_alloc_zeroed(uacpi_size size) { return lib::allocz(size); }
#endif

#ifndef UACPI_SIZED_FREES
    void uacpi_kernel_free(void *mem) { lib::free(mem); }
#else
    void uacpi_kernel_free(void *mem, uacpi_size) { lib::free(mem); }
#endif

    void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char *buf)
    {
        // remove \n
        std::string_view str { buf, std::strlen(buf) - 1 };
        switch (lvl)
        {
            case UACPI_LOG_DEBUG:
            case UACPI_LOG_TRACE:
                lib::debug("uacpi: {}", str);
                break;
            case UACPI_LOG_INFO:
                lib::info("uacpi: {}", str);
                break;
            case UACPI_LOG_WARN:
                lib::warn("uacpi: {}", str);
                break;
            case UACPI_LOG_ERROR:
                lib::error("uacpi: {}", str);
                break;
            default:
                std::unreachable();
        }
    }

    uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot()
    {
        const auto clock = chrono::main_clock();
        if (clock == nullptr)
            return 0;
        return clock->ns();
    }

    void uacpi_kernel_stall(uacpi_u8 usec)
    {
        chrono::stall_ns(usec * 1'000);
    }

    void uacpi_kernel_sleep(uacpi_u64 msec)
    {
        if (sched::is_initialised())
            sched::sleep_for(msec);
        else
            uacpi_kernel_stall(msec * 1'000);
    }

    uacpi_handle uacpi_kernel_create_mutex()
    {\
        return reinterpret_cast<uacpi_handle>(new lib::mutex);
    }

    void uacpi_kernel_free_mutex(uacpi_handle handle)
    {
        delete reinterpret_cast<lib::mutex *>(handle);
    }

    struct simple_event
    {
        std::atomic<size_t> counter;

        bool decrement()
        {
            while (true)
            {
                auto value = counter.load(std::memory_order::acquire);
                if (value == 0)
                    return false;

                if (counter.compare_exchange_strong(value, value - 1, std::memory_order::acq_rel, std::memory_order::acquire))
                    return true;
            }
        }
    };

    uacpi_handle uacpi_kernel_create_event()
    {
        return reinterpret_cast<uacpi_handle>(new simple_event);
    }

    void uacpi_kernel_free_event(uacpi_handle handle)
    {
        delete reinterpret_cast<simple_event *>(handle);
    }

    uacpi_thread_id uacpi_kernel_get_thread_id()
    {
        if (sched::is_initialised())
        {
            const auto thread = sched::this_thread();
            return reinterpret_cast<uacpi_thread_id>(lib::unique_from(
                static_cast<std::size_t>(thread->tid), static_cast<std::size_t>(thread->parent->pid)
            ));
        }

        return reinterpret_cast<uacpi_thread_id>(1);
    }

    uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout)
    {
        auto *mutex = reinterpret_cast<lib::mutex *>(handle);
        bool locked = false;

        if (timeout == 0xFFFF)
        {
            mutex->lock();
            return UACPI_STATUS_OK;
        }
        else if (timeout == 0x0000)
            locked = mutex->try_lock();
        else
            locked = mutex->try_lock_until(static_cast<std::size_t>(timeout) * 1'000'000);

        return locked ? UACPI_STATUS_OK : UACPI_STATUS_TIMEOUT;
    }

    void uacpi_kernel_release_mutex(uacpi_handle handle)
    {
        auto *mutex = reinterpret_cast<lib::mutex *>(handle);
        mutex->unlock();
    }

    uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout)
    {
        auto event = reinterpret_cast<simple_event *>(handle);
        if (timeout == 0xFFFF)
        {
            while (!event->decrement())
                uacpi_kernel_sleep(10);
        }
        else
        {
            std::int32_t value = timeout;
            while (!event->decrement())
            {
                if (value <= 0)
                    return UACPI_FALSE;
                uacpi_kernel_sleep(10);
                value -= 10;
            }
        }
        return UACPI_TRUE;
    }

    void uacpi_kernel_signal_event(uacpi_handle handle)
    {
        auto event = reinterpret_cast<simple_event *>(handle);
        event->counter.fetch_add(1, std::memory_order_acq_rel);
    }

    void uacpi_kernel_reset_event(uacpi_handle handle)
    {
        auto event = reinterpret_cast<simple_event *>(handle);
        event->counter.store(0, std::memory_order_release);
    }

    uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req)
    {
        switch (req->type)
        {
            case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
                lib::info("uACPI: ignoring breakpoint");
                break;
            case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
                lib::error("fatal firmware error: type: 0x{:X} code: 0x{:X} arg: 0x{:X}",
                    static_cast<int>(req->fatal.type), req->fatal.code, req->fatal.arg
                );
                break;
            default:
                std::unreachable();
        }
        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler func, uacpi_handle ctx, uacpi_handle *out_irq_handle)
    {
#if defined(__x86_64__)
        const auto vector = irq + 0x20;
#else
        const auto vector = irq;
#endif

        auto handler = interrupts::get(cpu::bsp_idx(), vector).value();
        if (handler.get().used()) [[unlikely]]
            lib::panic("requested uACPI interrupt vector {} is already in use", vector);

        handler.get().set([](cpu::registers *, auto func, auto ctx) { func(ctx); }, func, ctx);
        interrupts::unmask(vector);

        *reinterpret_cast<std::size_t *>(out_irq_handle) = vector;

        lib::debug("uacpi: installed interrupt handler for irq {} (vector {})", irq, vector);
        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler, uacpi_handle irq_handle)
    {
        const auto vector = reinterpret_cast<std::size_t>(irq_handle);
        interrupts::mask(vector);

        auto handler = interrupts::get(cpu::bsp_idx(), vector).value();
        handler.get().reset();

        return UACPI_STATUS_OK;
    }

    uacpi_handle uacpi_kernel_create_spinlock()
    {
        return reinterpret_cast<uacpi_handle>(new lib::spinlock_irq);
    }

    void uacpi_kernel_free_spinlock(uacpi_handle handle)
    {
        delete reinterpret_cast<lib::spinlock_irq *>(handle);
    }

    uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle)
    {
        reinterpret_cast<lib::spinlock_irq *>(handle)->lock();
        return 0;
    }

    void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags)
    {
        reinterpret_cast<lib::spinlock_irq *>(handle)->unlock();
    }

    uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx)
    {
        lib::debug("uacpi: scheduling work of type {}", magic_enum::enum_name(type));
        switch (type)
        {
            case UACPI_WORK_GPE_EXECUTION:
                uacpi::gpe.write_lock()->emplace_back(handler, ctx);
                uacpi::added.signal();
                break;
            case UACPI_WORK_NOTIFICATION:
                uacpi::notify.write_lock()->emplace_back(handler, ctx);
                uacpi::added.signal();
                break;
            default:
                return UACPI_STATUS_INVALID_ARGUMENT;
        }
        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_wait_for_work_completion()
    {
        if (!uacpi::is_empty())
            uacpi::completed.wait();
        return UACPI_STATUS_OK;
    }
} // extern "C"