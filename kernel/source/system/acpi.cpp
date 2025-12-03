// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/uacpi.h>
#include <uacpi/tables.h>
#include <uacpi/context.h>
#include <uacpi/acpi.h>
#include <uacpi/utilities.h>
#include <uacpi/event.h>
#include <uacpi/notify.h>
#include <uacpi/sleep.h>

module system.acpi;

// TODO: maybe move arch dependent code to a separate file
#if defined(__x86_64__)
import x86_64.system.ioapic;
#endif

import drivers.timers;
import drivers.output;
import system.pci;
import arch;
import boot;
import lib;
import std;

namespace acpi
{
    namespace
    {
        constexpr std::size_t early_table_buffer_size = 1024;
        std::uint8_t *early_table_buffer;

#if defined(__x86_64__)
        void parse_madt()
        {
            uacpi_table out_table;
            if (uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &out_table) != UACPI_STATUS_OK)
                return;

            const auto ptr = static_cast<acpi_madt *>(out_table.ptr);
            madt::hdr = lib::alloc<acpi_madt *>(ptr->hdr.length);
            std::memcpy(madt::hdr, ptr, ptr->hdr.length);
            uacpi_table_unref(&out_table);

            const auto start = reinterpret_cast<std::uintptr_t>(madt::hdr->entries);
            const auto end = reinterpret_cast<std::uintptr_t>(madt::hdr) + madt::hdr->hdr.length;

            auto madt = reinterpret_cast<acpi_entry_hdr *>(start);

            for (auto entry = start; entry < end; entry += madt->length, madt = reinterpret_cast<acpi_entry_hdr *>(entry))
            {
                switch (madt->type)
                {
                    case ACPI_MADT_ENTRY_TYPE_LAPIC:
                        madt::lapics.push_back(*reinterpret_cast<acpi_madt_lapic *>(entry));
                        break;
                    case ACPI_MADT_ENTRY_TYPE_IOAPIC:
                        madt::ioapics.push_back(*reinterpret_cast<acpi_madt_ioapic *>(entry));
                        break;
                    case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE:
                        madt::isos.push_back(*reinterpret_cast<acpi_madt_interrupt_source_override *>(entry));
                        break;
                    case ACPI_MADT_ENTRY_TYPE_LOCAL_X2APIC:
                        madt::x2apics.push_back(*reinterpret_cast<acpi_madt_x2apic *>(entry));
                        break;
                }
            }
        }
#endif

        // TODO
        void shutdown()
        {
            log::info("acpi: trying to enter s5...");

            uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
            uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
        }
    } // namespace

    std::uintptr_t get_rsdp()
    {
        static const auto cached = [] { return boot::requests::rsdp.response->address; } ();
        return reinterpret_cast<std::uintptr_t>(cached);
    }

    lib::initgraph::stage *tables_stage()
    {
        static lib::initgraph::stage stage
        {
            "acpi.tables-access",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "acpi.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *workers_stage()
    {
        static lib::initgraph::stage stage
        {
            "acpi.workers-created",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task full_task
    {
        "acpi.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { tables_stage(), timers::initialised_stage() },
        lib::initgraph::entail { initialised_stage() },
        [] {
            delete[] early_table_buffer;

            uacpi_status ret = UACPI_STATUS_OK;
            auto check = [ret]
            {
                if (ret != UACPI_STATUS_OK) [[unlikely]]
                    lib::panic("could not initialise ACPI: {}", uacpi_status_to_string(ret));
            };

            ret = uacpi_initialize(0); check();
            ret = uacpi_namespace_load(); check();

#if defined(__x86_64__)
            const auto intmodel = x86_64::apic::io::is_initialised()
                ? UACPI_INTERRUPT_MODEL_IOAPIC
                : UACPI_INTERRUPT_MODEL_PIC;
            ret = uacpi_set_interrupt_model(intmodel); check();
#endif

            // TODO: ec

            ret = uacpi_namespace_initialize(); check();
            ret = uacpi_finalize_gpe_initialization(); check();

            ret = uacpi_install_fixed_event_handler(
                UACPI_FIXED_EVENT_POWER_BUTTON,
                [](uacpi_handle) -> uacpi_interrupt_ret
                {
                    uacpi_kernel_schedule_work(
                        UACPI_WORK_GPE_EXECUTION,
                        [](uacpi_handle) { shutdown(); }, nullptr
                    );
                    return UACPI_INTERRUPT_HANDLED;
                }, nullptr
            );
            check();

            ret = uacpi_find_devices("PNP0C0C",
                [](void *, uacpi_namespace_node *node, uacpi_u32)
                {
                    uacpi_install_notify_handler(node,
                        [](uacpi_handle, uacpi_namespace_node *, uacpi_u64 value) -> uacpi_status
                        {
                            if (value != 0x80)
                                return UACPI_STATUS_OK;

                            shutdown();
                            return UACPI_STATUS_OK;
                        }, nullptr
                    );
                    return UACPI_ITERATION_DECISION_CONTINUE;
                }, nullptr
            );
            check();
        }
    };

    lib::initgraph::task early_task
    {
        "acpi.early-tables",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { lib::initgraph::base_stage() },
        lib::initgraph::entail { tables_stage() },
        [] {
            log::info("acpi: setting up early table access");

            early_table_buffer = new std::uint8_t[early_table_buffer_size];

            uacpi_context_set_log_level(UACPI_LOG_INFO);
            uacpi_setup_early_table_access(early_table_buffer, early_table_buffer_size);

            uacpi_table_fadt(&fadt);

#if defined(__x86_64__)
            parse_madt();
#endif
        }
    };
} // namespace acpi