// Copyright (C) 2024-2026  ilobilo

#pragma once

#define __mod_concat_(x, y) x ## y
#define __mod_concat(x, y) __mod_concat_(x, y)
#define __mod_unique __mod_concat(__mod_concat(__ILOBILIX_MODULE__, __COUNTER__), __LINE__)

#ifdef __ILOBILIX_EXTERNAL_MODULE__
#  define __mod_single_module_guard extern "C" [[gnu::used]] void __single_module_guard() { }
#  define __mod_modinfo_prefix(name) ""
#  define __mod_define_modinfo_name(name) __mod_define_modinfo(name, "name", name);
#else
#  define __mod_single_module_guard
#  define __mod_modinfo_prefix(name) name "."
#  define __mod_define_modinfo_name(name)
#endif

#define __mod_define_modinfo_(value)                         \
    [[gnu::used, gnu::section(".modinfo"), gnu::aligned(1)]] \
    constexpr auto __mod_unique = value

#define __mod_define_modinfo_str(name, value)                        \
    [[gnu::used, gnu::section(".modinfo"), gnu::aligned(1)]]         \
    constexpr char __mod_unique[] = __mod_modinfo_prefix(name) value

#define __mod_define_modinfo(name, tag, info)    \
    __mod_define_modinfo_str(name, tag "=" info)

#define __mod_define_module(name, desc, ...)                           \
    __mod_single_module_guard;                                         \
    [[gnu::used, gnu::section(".modules"), gnu::aligned(8)]]           \
    constexpr ::mod::declare __mod_unique { name, desc, __VA_ARGS__ }; \
    __mod_define_modinfo_name(name)                                    \
    __mod_define_modinfo(name, "description", desc)

#define generic_module(name, desc, init, fini, ...)                    \
    __mod_define_module(                                               \
        name, desc,                                                    \
        ::mod::type::generic, (init), (fini),                          \
        ::mod::no_match                                                \
        __VA_OPT__(, ::mod::deps { __VA_ARGS__ })                      \
    )                                                                  \
    __VA_OPT__(;__mod_define_modinfo_(                                 \
        ::mod::format_depends<__mod_modinfo_prefix(name)>(__VA_ARGS__) \
    ))

#define filesystem_module(name, desc, fs, ...)                         \
    __mod_define_module(                                               \
        name, desc, ::mod::type::filesystem,                           \
        +[] { return bool(::vfs::register_fs(fs)); },                  \
        +[] { return bool(::vfs::unregister_fs(fs)); },                \
        ::mod::string_match<"fs-" name>()                              \
        __VA_OPT__(, ::mod::deps { __VA_ARGS__ })                      \
    );                                                                 \
    __mod_define_modinfo(name, "alias", "fs-" name)                    \
    __VA_OPT__(;__mod_define_modinfo_(                                 \
        ::mod::format_depends<__mod_modinfo_prefix(name)>(__VA_ARGS__) \
    ))

#define device_module(type, name, desc, drv, ids, ...)                                \
    __mod_define_module(                                                              \
        name, desc, type,                                                             \
        +[] { return bool(::dev::register_driver(drv)); },                            \
        +[] { return bool(::dev::unregister_driver(drv)); },                          \
        ::mod::ids_match(::mod::get_modaliases_array<ids>())                          \
        __VA_OPT__(, ::mod::deps { __VA_ARGS__ })                                     \
    );                                                                                \
    __mod_define_modinfo_((::mod::get_modaliases<__mod_modinfo_prefix(name), ids>())) \
    __VA_OPT__(;__mod_define_modinfo_(                                                \
        ::mod::format_depends<__mod_modinfo_prefix(name)>(__VA_ARGS__)                \
    ))

#define pci_module(name, desc, drv, ids, ...)                          \
    device_module(::mod::type::pci, name, desc, drv, ids, __VA_ARGS__)

#define acpi_module(name, desc, drv, hids, ...)                          \
    device_module(::mod::type::acpi, name, desc, drv, hids, __VA_ARGS__)
