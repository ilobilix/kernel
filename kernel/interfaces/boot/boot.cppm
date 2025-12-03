// Copyright (C) 2024-2025  ilobilo

module;

#include <limine.h>

export module boot;

import lib;
import std;

namespace
{
    [[gnu::used, gnu::section(".limine_requests_start")]]
    volatile std::uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

    [[gnu::used, gnu::section(".limine_requests_end")]]
    volatile std::uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

} // namespace

export namespace boot
{
    // constexpr std::uintptr_t kstack_size = 0x10000; // 64 kib
    constexpr std::uintptr_t kstack_size = 0x4000; // 16 kib
    constexpr std::uintptr_t ustack_size = 0x200000; // 2 mib

#if ILOBILIX_LIMINE_MP
    using limine_mp_info = ::limine_mp_info;
#endif
    using limine_memmap_entry = ::limine_memmap_entry;

    enum class memmap : std::uint64_t
    {
        usable = LIMINE_MEMMAP_USABLE,
        reserved = LIMINE_MEMMAP_RESERVED,
        acpi_reclaimable = LIMINE_MEMMAP_ACPI_RECLAIMABLE,
        acpi_nvs = LIMINE_MEMMAP_ACPI_NVS,
        bad_memory = LIMINE_MEMMAP_BAD_MEMORY,
        bootloader = LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE,
        kernel_and_modules = LIMINE_MEMMAP_EXECUTABLE_AND_MODULES,
        framebuffer = LIMINE_MEMMAP_FRAMEBUFFER,
        acpi_tables = LIMINE_MEMMAP_ACPI_TABLES
    };

    [[gnu::used, gnu::section(".limine_requests")]]
    volatile std::uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(4);

    namespace requests
    {
// #if defined(__aarch64__)
//         [[gnu::used, gnu::section(".limine_requests")]]
//         volatile limine_dtb_request dtb
//         {
//             .id = LIMINE_DTB_REQUEST,
//             .revision = 0,
//             .response = nullptr
//         };
// #endif

        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_framebuffer_request framebuffer
        {
            .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
            .revision = 0,
            .response = nullptr
        };

#if defined(__x86_64__)
#  define LIMINE_PAGING_MODE_DEFAULT LIMINE_PAGING_MODE_X86_64_DEFAULT
#elif defined(__aarch64__)
#  define LIMINE_PAGING_MODE_DEFAULT LIMINE_PAGING_MODE_AARCH64_DEFAULT
#endif

        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_paging_mode_request paging_mode
        {
            .id = LIMINE_PAGING_MODE_REQUEST_ID,
            .revision = 0,
            .response = nullptr,
            .mode = LIMINE_PAGING_MODE_DEFAULT,
            .max_mode = LIMINE_PAGING_MODE_DEFAULT,
            .min_mode = LIMINE_PAGING_MODE_DEFAULT
        };

        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_memmap_request memmap
        {
            .id = LIMINE_MEMMAP_REQUEST_ID,
            .revision = 0,
            .response = nullptr
        };

        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_rsdp_request rsdp
        {
            .id = LIMINE_RSDP_REQUEST_ID,
            .revision = 0,
            .response = nullptr
        };

        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_module_request module_
        {
            .id = LIMINE_MODULE_REQUEST_ID,
            .revision = 0,
            .response = nullptr,
            .internal_module_count = 0,
            .internal_modules = nullptr
        };

        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_executable_file_request kernel_file
        {
            .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
            .revision = 0,
            .response = nullptr
        };

        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_date_at_boot_request boot_time
        {
            .id = LIMINE_DATE_AT_BOOT_REQUEST_ID,
            .revision = 0,
            .response = nullptr
        };

        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_hhdm_request hhdm
        {
            .id = LIMINE_HHDM_REQUEST_ID,
            .revision = 0,
            .response = nullptr
        };

        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_executable_address_request kernel_address
        {
            .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
            .revision = 0,
            .response = nullptr
        };

#if ILOBILIX_LIMINE_MP
        [[gnu::used, gnu::section(".limine_requests")]]
        volatile limine_mp_request mp
        {
            .id = LIMINE_MP_REQUEST_ID,
            .revision = 0,
            .response = nullptr,
#  if defined(__x86_64__)
            .flags = LIMINE_MP_REQUEST_X86_64_X2APIC
#  else
            .flags = 0
#  endif
        };
#endif
    } // namespace requests

    limine_file *find_module(std::string_view name)
    {
        const auto mods = requests::module_.response;
        for (std::size_t i = 0; i < mods->module_count; i++)
        {
            if (mods->modules[i]->string == name)
                return mods->modules[i];
        }
        return nullptr;
    }

    std::uintptr_t get_hhdm_offset()
    {
        static const auto cached = [] { return requests::hhdm.response->offset; } ();
        return cached;
    }


    std::int64_t time()
    {
        static const auto cached = [] { return requests::boot_time.response->timestamp; } ();
        return cached;
    }

    void check_requests()
    {
        if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
            lib::panic("Limine base revision not supported");

        if (requests::memmap.response == nullptr)
            lib::panic("could not get a response to the memmap request");
        if (requests::paging_mode.response == nullptr)
            lib::panic("could not get a response to the paging mode request");
        if (requests::hhdm.response == nullptr)
            lib::panic("could not get a response to the hhdm request");
        if (requests::kernel_file.response == nullptr)
            lib::panic("could not get a response to the kernel file request");
        if (requests::kernel_address.response == nullptr)
            lib::panic("could not get a response to the kernel address request");
        if (requests::framebuffer.response == nullptr)
            lib::panic("could not get a response to the framebuffer request");
        if (requests::rsdp.response == nullptr)
            lib::panic("could not get a response to the rsdp request");
        if (requests::module_.response == nullptr)
            lib::panic("could not get a response to the module request");
        if (requests::boot_time.response == nullptr)
            lib::panic("could not get a response to the boot time request");
#if ILOBILIX_LIMINE_MP
        if (requests::mp.response == nullptr)
            lib::panic("could not get a response to the mp request");
#endif

// #if defined(__aarch64__)
//         if (requests::dtb.response == nullptr)
//             lib::panic("could not get a response to the dtb request");
// #endif

        get_hhdm_offset(); time();
    }
} // export namespace boot

// used in math.cppm
extern "C++" std::uintptr_t (*get_hhdm_offset)() = &boot::get_hhdm_offset;