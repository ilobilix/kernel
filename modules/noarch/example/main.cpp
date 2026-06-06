// Copyright (C) 2024-2026  ilobilo

import system.pci;
import system.dev;
import lib;

import std;
import std.compat;

namespace generic
{
    // constructors are ran on first driver init
    // destructors are ran on last driver fini
    __attribute__((constructor))
    void func()
    {
        lib::info("YAYAYAYAYAYAYAYAYYAYAYAY!");
        lib::print("abcd");
        lib::println("efg");
        lib::debug("hijklmnop");
    }

    bool init() { lib::error("Hello, World!"); return true; }
    bool fini() { lib::warn("Goodbye, World!"); return true; }
} // namespace generic

// generic drivers are always activated, even if nothing depends on them
generic_module(
    "generic-driver", "a generic module description",
    generic::init, generic::fini
);

namespace pci
{
    constexpr pci::id_t drv_ids[] {
        { 0xDEAD, 0xBEEF, 0xFFFFFFFF, 0 },
        { 0xB16B, 0x00B5, 0xFFFFFFFF, 0 }
    };

    struct pci_drv : pci::driver_t
    {
        pci_drv() : pci::driver_t { "pci-driver", drv_ids } { }

        lib::expect<void> probe(dev::device_t &dev) override
        {
            lib::unused(dev);
            lib::info("probing pci-driver");
            return { }; // success
        }

        lib::expect<void> remove(dev::device_t &dev) override
        {
            lib::unused(dev);
            lib::info("removing pci-driver");
            return { };
        }
    } driver;
} // namespace pci

pci_module(
    "pci-driver", "a pci driver description",
    pci::driver, pci::drv_ids,
    mod::deps { "generic-driver" }
);

// TODO: acpi_module(name, desc, drv, hids, ...)
