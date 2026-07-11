// Copyright (C) 2024-2026  ilobilo

// pci_module requires these imports
import system.pci;
import system.dev;
import lib;

namespace pci
{
    constexpr pci::id_t drv_ids[] {
        id_t::from_id(0xDEAD, 0xBEEF),
        id_t::from_id(0xB16B, 0x00B5),
        id_t::from_class(0x69, 0x42, 0x00) // 4th arg class_mask is id_t::any by default
    };

    struct pci_drv : pci::driver_t
    {
        pci_drv() : pci::driver_t { "pci-driver", drv_ids } { }

        lib::expect<void> probe(pci::device_t &dev) override
        {
            lib::unused(dev);
            lib::info("probing pci-driver");
            return { }; // success
        }

        bool remove(pci::device_t &dev) override
        {
            lib::unused(dev);
            lib::info("removing pci-driver");
            return true;
        }
    } driver;
} // namespace pci

pci_module(
    "pci-driver", "a pci driver description",
    pci::driver, pci::drv_ids,
    "generic-driver", "nvme" // deps
);

// other types:
//   generic_module(name, desc, init, fini, ...)
//   filesystem_module(name, desc, fs, ...)
//   acpi_module(name, desc, drv, hids, ...)
