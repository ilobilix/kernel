// Copyright (C) 2024  ilobilo

#include <uacpi/kernel_api.h>

import ilobilix;
import std;

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

    uacpi_status uacpi_kernel_pci_read(uacpi_handle device, uacpi_size offset,uacpi_u8 byte_width, uacpi_u64 *value)
    {
        auto dev = reinterpret_cast<pci_dev *>(device);
        *value = dev->io->read(dev->addr.segment, dev->addr.bus, dev->addr.device, dev->addr.function, offset, byte_width);
        return UACPI_STATUS_OK;
    }
    uacpi_status uacpi_kernel_pci_write(uacpi_handle device, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64 value)
    {
        auto dev = reinterpret_cast<pci_dev *>(device);
        dev->io->write(dev->addr.segment, dev->addr.bus, dev->addr.device, dev->addr.function, offset, value, byte_width);
        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size, uacpi_handle *out_handle)
    {
        *out_handle = reinterpret_cast<uacpi_handle>(base);
        return UACPI_STATUS_OK;
    }

    void uacpi_kernel_io_unmap(uacpi_handle) { }

    uacpi_status uacpi_kernel_io_read(uacpi_handle handle, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64 *value)
    {
        if constexpr (lib::io::supported)
        {
            auto address = reinterpret_cast<std::size_t>(handle) + offset;
            switch (byte_width)
            {
                case sizeof(std::uint8_t):
                    *value = lib::io::in<8>(address);
                    break;
                case sizeof(std::uint16_t):
                    *value = lib::io::in<16>(address);
                    break;
                case sizeof(std::uint32_t):
                    *value = lib::io::in<32>(address);
                    break;
                default:
                    std::unreachable();
            }
            return UACPI_STATUS_OK;
        }
        return UACPI_STATUS_UNIMPLEMENTED;
    }

    uacpi_status uacpi_kernel_io_write(uacpi_handle handle, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64 value)
    {
        if constexpr (lib::io::supported)
        {
            auto address = reinterpret_cast<std::size_t>(handle) + offset;
            switch (byte_width)
            {
                case sizeof(std::uint8_t):
                    lib::io::out<8>(address, value);
                    break;
                case sizeof(std::uint16_t):
                    lib::io::out<16>(address, value);
                    break;
                case sizeof(std::uint32_t):
                    lib::io::out<32>(address, value);
                    break;
                default:
                    std::unreachable();
            }
            return UACPI_STATUS_OK;
        }
        return UACPI_STATUS_UNIMPLEMENTED;
    }

    void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
    {
        auto &pmap = vmm::kernel_pagemap;

        // const auto psize = vmm::pagemap::max_page_size(len);
        const auto psize = vmm::page_size::small;
        const auto npsize = vmm::pagemap::from_page_size(psize);

        const auto paddr = lib::align_down(addr, npsize);
        const auto size = lib::align_up((addr - paddr) + len, npsize);

        const auto vaddr = lib::fromhh(vmm::alloc_vpages(vmm::vspace::acpi, lib::div_roundup(size, pmm::page_size)));

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
        time::sleep_ns(msec * 1'000'000);
    }

    uacpi_handle uacpi_kernel_create_mutex()
    {
        return reinterpret_cast<uacpi_handle>(new std::mutex);
    }

    void uacpi_kernel_free_mutex(uacpi_handle handle)
    {
        delete reinterpret_cast<std::mutex *>(handle);
    }

    uacpi_handle uacpi_kernel_create_event()
    {
        return reinterpret_cast<uacpi_handle>(new lib::simple_event);
    }

    void uacpi_kernel_free_event(uacpi_handle handle)
    {
        delete reinterpret_cast<lib::simple_event *>(handle);
    }

    // TODO
    uacpi_thread_id uacpi_kernel_get_thread_id() { return reinterpret_cast<uacpi_thread_id>(1); }

    uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout)
    {
        auto *mutex = reinterpret_cast<std::mutex *>(handle);
        bool locked = false;

        if (timeout == 0xFFFF)
        {
            mutex->lock();
            return UACPI_STATUS_OK;
        }
        else if (timeout == 0x0000)
            locked = mutex->try_lock();
        else
            locked = mutex->try_lock_until(timeout * 1'000'000);

        return locked ? UACPI_STATUS_OK : UACPI_STATUS_TIMEOUT;
    }

    void uacpi_kernel_release_mutex(uacpi_handle handle)
    {
        auto *mutex = reinterpret_cast<std::mutex *>(handle);
        mutex->unlock();
    }

    uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout)
    {
        auto event = reinterpret_cast<lib::simple_event *>(handle);
        if (timeout == 0xFFFF)
            event->await();
        else
            return event->await_timeout(timeout * 1'000'000) ? UACPI_TRUE : UACPI_FALSE;
        return UACPI_TRUE;
    }

    void uacpi_kernel_signal_event(uacpi_handle handle)
    {
        auto event = reinterpret_cast<lib::simple_event *>(handle);
        event->trigger();
    }

    void uacpi_kernel_reset_event(uacpi_handle handle)
    {
        auto event = reinterpret_cast<lib::simple_event *>(handle);
        if (event->drop())
        {
            while (event->num_awaiters())
                event->trigger();
        }
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

        auto handler = interrupts::get(cpu::bsp_idx, vector).value();
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

        auto handler = interrupts::get(cpu::bsp_idx, vector).value();
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

    // TODO
    /*
    * Schedules deferred work for execution.
    * Might be invoked from an interrupt context.
    */
    uacpi_status uacpi_kernel_schedule_work(
        uacpi_work_type, uacpi_work_handler, uacpi_handle
    ) { return UACPI_STATUS_UNIMPLEMENTED; }

    // TODO
    /*
    * Blocks until all scheduled work is complete and the work queue becomes empty.
    */
    uacpi_status uacpi_kernel_wait_for_work_completion()
    {
        return UACPI_STATUS_UNIMPLEMENTED;
    }
} // extern "C"