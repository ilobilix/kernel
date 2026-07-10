// Copyright (C) 2024-2026  ilobilo

#pragma once

#define __mod_concat_(x, y) x ## y
#define __mod_concat(x, y) __mod_concat_(x, y)
#define __mod_unique __mod_concat(__mod_concat(__MODULE_NAME__, __COUNTER__), __LINE__)

#define define_module                                        \
    [[gnu::used, gnu::section(".modules"), gnu::aligned(8)]] \
    constexpr ::mod::declare __mod_unique

#define generic_module(name, desc, init, fini, ...)       \
    define_module {                                       \
        name, desc, ::mod::type::generic, (init), (fini), \
        ::mod::no_match __VA_OPT__(,) __VA_ARGS__         \
    }

#define filesystem_module(name, desc, fs, ...)                \
    define_module {                                           \
        name, desc, ::mod::type::filesystem,                  \
        +[] { return bool(::vfs::register_fs(fs)); },         \
        +[] { return bool(::vfs::unregister_fs(fs)); },       \
        ::mod::string_match<name>() __VA_OPT__(,) __VA_ARGS__ \
    }

#define pci_module(name, desc, drv, ids, ...)                \
    define_module {                                          \
        name, desc, ::mod::type::pci,                        \
        +[] { return bool(::dev::register_driver(drv)); },   \
        +[] { return bool(::dev::unregister_driver(drv)); }, \
        ::mod::ids_match(ids) __VA_OPT__(,) __VA_ARGS__      \
    }

#define acpi_module(name, desc, drv, hids, ...)               \
    define_module {                                           \
        name, desc, ::mod::type::acpi,                        \
        +[] { return bool(::acpi::register_driver(drv)); },   \
        +[] { return bool(::acpi::unregister_driver(drv)); }, \
        ::mod::ids_match(hids) __VA_OPT__(,) __VA_ARGS__      \
    }
