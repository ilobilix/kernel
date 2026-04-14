// Copyright (C) 2024  ilobilo

module;

#include <uacpi/uacpi.h>
#include <uacpi/tables.h>
#include <uacpi/acpi.h>
#include <uacpi/utilities.h>
#include <uacpi/event.h>
#include <uacpi/notify.h>

module system.acpi;

// TODO: maybe move arch dependent code to a separate file
#if defined(__x86_64__)
import x86_64.system.ioapic;
#endif

import drivers.timers.acpipm;
import boot;
import lib;
import std;

namespace acpi
{
    namespace
    {
        constexpr std::size_t early_table_buffer_size = 1024;
        std::uint8_t early_table_buffer[early_table_buffer_size];

        void parse_madt()
        {
            uacpi_table out_table;
            if (uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &out_table) != UACPI_STATUS_OK)
                return;

            madt::hdr = new acpi_madt;
            auto ptr = static_cast<acpi_madt *>(out_table.ptr);
            std::memcpy(madt::hdr, ptr, ptr->hdr.length);
            uacpi_table_unref(&out_table);

            auto start = reinterpret_cast<std::uintptr_t>(madt::hdr->entries);
            auto end = reinterpret_cast<std::uintptr_t>(madt::hdr) + madt::hdr->hdr.length;

            auto madt = reinterpret_cast<acpi_entry_hdr *>(start);

            for (auto entry = start; entry < end; entry += madt->length, madt = reinterpret_cast<acpi_entry_hdr *>(entry))
            {
                switch (madt->type)
                {
                    case 1:
                        madt::ioapics.push_back(*reinterpret_cast<acpi_madt_ioapic *>(entry));
                        break;
                    case 2:
                        madt::isos.push_back(*reinterpret_cast<acpi_madt_interrupt_source_override *>(entry));
                        break;
                }
            }
        }
    } // namespace

    std::uintptr_t get_rsdp()
    {
        static const auto cached = [] { return boot::requests::rsdp.response->address; } ();
        return reinterpret_cast<std::uintptr_t>(cached);
    }

    void init()
    {
        uacpi_status ret = UACPI_STATUS_OK;
        auto check = [ret]
        {
            if (ret != UACPI_STATUS_OK) [[unlikely]]
                lib::panic("Could not initialise ACPI: {}", uacpi_status_to_string(ret));
        };

        ret = uacpi_initialize(0); check();
        ret = uacpi_namespace_load(); check();

#if defined(__x86_64__)
        auto intmodel = x86_64::apic::io::initialised ? UACPI_INTERRUPT_MODEL_IOAPIC : UACPI_INTERRUPT_MODEL_PIC;
        ret = uacpi_set_interrupt_model(intmodel); check();
#endif

        // TODO: ec

        ret = uacpi_namespace_initialize(); check();
        ret = uacpi_finalize_gpe_initialization(); check();

        // ret = uacpi_install_fixed_event_handler(
        //     UACPI_FIXED_EVENT_POWER_BUTTON,
        //     [](uacpi_handle) -> uacpi_interrupt_ret
        //     {
        //         uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, [](uacpi_handle) { arch::shutdown(); }, nullptr);
        //         return UACPI_INTERRUPT_HANDLED;
        //     }, nullptr
        // );
        // check();

        // ret = uacpi_find_devices("PNP0C0C",
        //     [](void *, uacpi_namespace_node *node)
        //     {
        //         uacpi_install_notify_handler(node, [](uacpi_handle, uacpi_namespace_node *, uacpi_u64 value) -> uacpi_status
        //             {
        //                 // 0x80: S0 Power Button Pressed
        //                 if (value != 0x80)
        //                     return UACPI_STATUS_OK;

        //                 arch::shutdown();

        //                 return UACPI_STATUS_OK;
        //             }, nullptr
        //         );
        //         return UACPI_NS_ITERATION_DECISION_CONTINUE;
        //     }, nullptr
        // );
        // check();

        if (timers::acpipm::supported())
        {
            ret = uacpi_install_fixed_event_handler(
                UACPI_FIXED_EVENT_TIMER_STATUS,
                [](uacpi_handle) -> uacpi_interrupt_ret
                {
                    timers::acpipm::overflows++;
                    return UACPI_INTERRUPT_HANDLED;
                }, nullptr
            ); check();
            timers::acpipm::finalise();
        }
    }

    void early()
    {
        log::debug("Setting up ACPI early table access");
        uacpi_setup_early_table_access(early_table_buffer, early_table_buffer_size);

        parse_madt();
    }
} // namespace acpi