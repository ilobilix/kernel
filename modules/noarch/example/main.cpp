// Copyright (C) 2024-2026  ilobilo

// pci driver requires these imports
import drivers.pci;
import drivers.dev;
import lib;

namespace pci
{
    // types of match_ids and the base class should match as they determine the driver type
    struct pci_drv : pci::driver_t
    {
        // device_module needs this member
        static constexpr pci::id_t match_ids[] {
            id_t::from_id(0xDEAD, 0xBEEF),
            id_t::from_id(0xB16B, 0x00B5),
            id_t::from_class(0x69, 0x42, 0x00) // 4th arg class_mask is id_t::any by default
        };

        pci_drv() : pci::driver_t { "pci-driver", match_ids } { }

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

// can be pci, acpi, virtio, etc. type depends on driver match_ids member
device_module(
    "pci-driver", "a pci driver description",
    pci::driver,
    "nvme", "ext2" // deps
);

// other types:
//   filesystem_module(name, desc, fs, ...)
//   generic_module(name, desc, init, fini, ...)

// generic modules are only loaded if requested as dependencies
