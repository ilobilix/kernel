// Copyright (C) 2024-2025  ilobilo

#include <uacpi/kernel_api.h>

import ilobilix;
import magic_enum;
import cppstd;

namespace uacpi
{
    namespace
    {
        using queue_type = std::deque<std::pair<uacpi_work_handler, uacpi_handle>>;
        queue_type notify { };
        queue_type gpe { };

        std::atomic_size_t workers { 0 };
        lib::semaphore semaphore { };
        lib::spinlock<false> lock;
    } // namespace

    void worker_caller(queue_type *queue)
    {
        while (true)
        {
            while (queue->empty())
                sched::yield();

            lock.lock();
            auto [handler, handle] = queue->pop_front_element();
            lock.unlock();

            handler(handle);

            workers.fetch_sub(1, std::memory_order_acq_rel);
            semaphore.signal(0, true);
        }
    }

    void notify_worker()
    {
        worker_caller(&notify);
        arch::halt(true);
    }

    void gpe_worker()
    {
        worker_caller(&gpe);
        arch::halt(true);
    }

    void init_workers()
    {
        auto notify_thread = sched::thread::create(
            boot::pid0, reinterpret_cast<std::uintptr_t>(notify_worker)
        );
        auto gpe_thread = sched::thread::create(
            boot::pid0, reinterpret_cast<std::uintptr_t>(gpe_worker)
        );

        notify_thread->status = sched::status::ready;
        gpe_thread->status = sched::status::ready;

        sched::enqueue(notify_thread, cpu::self()->idx);
        sched::enqueue(gpe_thread, cpu::self()->idx);
    }
} // namespace uacpi

extern "C"
{
#ifdef UACPI_KERNEL_INITIALIZATION
    uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl)
    {
        log::debug("uacpi_kernel_initialize({})", static_cast<int>(current_init_lvl));
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
        log::debug("uacpi_kernel_deinitialize()");
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

        const auto vaddr = lib::fromhh(vmm::alloc_vpages(vmm::space_type::acpi, lib::div_roundup(size, pmm::page_size)));

        if (!pmap->map(vaddr, paddr, size, vmm::flag::rw, psize))
            lib::panic("could not map acpi memory");

        return reinterpret_cast<std::uint8_t *>(vaddr) + (addr - paddr);
    }

    void uacpi_kernel_unmap(void *addr, uacpi_size len)
    {
        auto &pmap = vmm::kernel_pagemap;

        const auto psize = vmm::page_size::small;
        const auto npsize = vmm::pagemap::from_page_size(psize);

        const auto paddr = reinterpret_cast<std::uintptr_t>(addr);
        const auto vaddr = lib::align_down(paddr, npsize);
        const auto size = lib::align_up((paddr - vaddr) + len, npsize);

        if (!pmap->unmap(vaddr, size, psize))
            lib::panic("could not unmap acpi memory");
    }

    void *uacpi_kernel_alloc(uacpi_size size) { return std::malloc(size); }

#ifdef UACPI_NATIVE_ALLOC_ZEROED
    void *uacpi_kernel_alloc_zeroed(uacpi_size size) { return std::calloc(1, size); }
#endif

#ifndef UACPI_SIZED_FREES
    void uacpi_kernel_free(void *mem) { std::free(mem); }
#else
    void uacpi_kernel_free(void *mem, uacpi_size) { std::free(mem); }
#endif

#ifndef UACPI_FORMATTED_LOGGING
    void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char *str)
    {
        switch (lvl)
        {
            case UACPI_LOG_DEBUG:
            case UACPI_LOG_TRACE:
                log::debug("uacpi: {}", str);
                break;
            case UACPI_LOG_INFO:
                log::info("uacpi: {}", str);
                break;
            case UACPI_LOG_WARN:
                log::warn("uacpi: {}", str);
                break;
            case UACPI_LOG_ERROR:
                log::error("uacpi: {}", str);
                break;
            default:
                std::unreachable();
        }
    }
#else
    UACPI_PRINTF_DECL(2, 3)
    void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char *str, ...)
    {
        va_list va;
        va_start(va, str);
        uacpi_kernel_vlog(lvl, str, va);
        va_end(va);
    }

    void uacpi_kernel_vlog(uacpi_log_level lvl, const uacpi_char *_str, uacpi_va_list va)
    {
        auto str = const_cast<uacpi_char *>(_str);
        const auto len = std::strlen(str);

        if (str[len - 1] == '\n')
            str[len - 1] = 0;
        // if (std::isalpha(str[0]))
        //     str[0] = std::toupper(str[0]);

        char *buffer;
        std::vasprintf(&buffer, str, va);
        switch (lvl)
        {
            case UACPI_LOG_DEBUG:
            case UACPI_LOG_TRACE:
                log::debug("uacpi: {}", buffer);
                break;
            case UACPI_LOG_INFO:
                log::info("uacpi: {}", buffer);
                break;
            case UACPI_LOG_WARN:
                log::warn("uacpi: {}", buffer);
                break;
            case UACPI_LOG_ERROR:
                log::error("uacpi: {}", buffer);
                break;
            default:
                std::unreachable();
        }
        std::free(buffer);
    }
#endif

    uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot()
    {
        const auto clock = time::main_clock();
        if (clock == nullptr)
            return 0;
        return clock->ns();
    }

    void uacpi_kernel_stall(uacpi_u8 usec)
    {
        time::stall_ns(usec * 1'000);
    }

    void uacpi_kernel_sleep(uacpi_u64 msec)
    {
        if (sched::initialised)
        {
            sched::this_thread()->prepare_sleep(msec);
            sched::yield();
        }
        else uacpi_kernel_stall(msec * 1'000);
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
        if (sched::initialised)
        {
            auto thread = sched::percpu->running_thread;
            auto proc = thread->proc.lock();
            return reinterpret_cast<uacpi_thread_id>(lib::unique_from(thread->tid, proc->pid));
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
                log::info("uACPI: ignoring breakpoint");
                break;
            case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
                log::error("fatal firmware error: type: 0x{:X} code: 0x{:X} arg: 0x{:X}",
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
        return reinterpret_cast<uacpi_handle>(new lib::spinlock<true>);
    }

    void uacpi_kernel_free_spinlock(uacpi_handle handle)
    {
        delete reinterpret_cast<lib::spinlock<true> *>(handle);
    }

    uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle)
    {
        reinterpret_cast<lib::spinlock<true> *>(handle)->lock();
        return 0;
    }

    void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags)
    {
        reinterpret_cast<lib::spinlock<true> *>(handle)->unlock();
    }

    uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx)
    {
        const std::unique_lock _ { uacpi::lock };
        log::debug("uacpi: scheduling work of type {}", magic_enum::enum_name(type));
        switch (type)
        {
            case UACPI_WORK_GPE_EXECUTION:
                uacpi::gpe.emplace_back(handler, ctx);
                uacpi::workers.fetch_add(1, std::memory_order_acq_rel);
                break;
            case UACPI_WORK_NOTIFICATION:
                uacpi::notify.emplace_back(handler, ctx);
                uacpi::workers.fetch_add(1, std::memory_order_acq_rel);
                break;
            default:
                return UACPI_STATUS_INVALID_ARGUMENT;
        }
        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_wait_for_work_completion()
    {
        while (uacpi::workers.load(std::memory_order_acquire))
            uacpi::semaphore.wait();

        return UACPI_STATUS_OK;
    }
} // extern "C"