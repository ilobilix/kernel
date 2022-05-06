// Copyright (C) 2024  ilobilo

#pragma once

#include <cstdint>

#include <uacpi/resources.h>
#include <uacpi/utilities.h>

enum class driver_type : uint8_t
{
    generic,
    acpi
};

struct [[gnu::packed]] generic_driver_t
{
    bool (*init)();
    bool (*fini)();

    std::size_t num_deps;
    const char **deps;
};

struct [[gnu::packed]] acpi_driver_t
{
    bool (*probe)(uacpi_namespace_node *node, uacpi_namespace_node_info *info);

    std::size_t num_pnp_ids;
    const char **pnp_ids;
};

struct [[gnu::packed, gnu::aligned(8)]] driver_t
{
    const char *name;
    bool initialised;
    bool failed;

    driver_type type;
    union {
        generic_driver_t generic;
        acpi_driver_t acpi;
    };
};

#define DRIVER_SECTION ".drivers"
#define DRIVER_DATA_SECTION ".drivers.data"

#define __DRIVER_internal(drv_name, drv_obj_name, drv_type, drv_struct...) \
    extern "C" [[gnu::section(DRIVER_SECTION), gnu::used]]                 \
    const driver_t drv_obj_name =                                          \
    {                                                                      \
        .name = drv_name,                                                  \
        .initialised = false,                                              \
        .failed = false,                                                   \
        .type = driver_type::drv_type,                                     \
        .drv_type = drv_struct                                             \
    };

// UTILITIES

#define __DRIVER_glue_impl(x, y) x ## y
#define __DRIVER_glue(x, y) __DRIVER_glue_impl(x, y)

#define __DRIVER_random(name) __DRIVER_glue(_, __DRIVER_glue(name ## _, __DRIVER_glue(__COUNTER__, __LINE__)))

#define __DRIVER_pragma(x) _Pragma(#x)

#define __DRIVER_rename_get(random, func) __DRIVER_glue(func, random)
#define __DRIVER_rename_impl(old_name, new_name)        \
    __DRIVER_pragma(redefine_extname old_name new_name) \
    extern "C" bool new_name();

#define __DRIVER_rename(random, func) __DRIVER_rename_impl(func, __DRIVER_rename_get(random, func))

#define __DRIVER_list(type, name, contents...)       \
    [[gnu::section(DRIVER_DATA_SECTION), gnu::used]] \
    type name contents;

#define __DRIVER_count(obj, type...) sizeof(obj) / sizeof(type)

// GENERIC

#define __GENERIC_DRIVER_internal(drv_obj_name, deps_obj_name, _init, _fini, drv_name, _deps...)                                                                          \
    __DRIVER_list(const char *, deps_obj_name[], { _deps })                                                                                                               \
    __DRIVER_internal(drv_name, drv_obj_name, generic, { .init = _init, .fini = _fini, .num_deps = __DRIVER_count(deps_obj_name, const char *), .deps = deps_obj_name })

#define __GENERIC_DRIVER_expand(random, name, init, fini, deps...) \
    __DRIVER_rename(random, init)                                  \
    __DRIVER_rename(random, fini)                                  \
    __GENERIC_DRIVER_internal(                                     \
        __DRIVER_glue(__generic_driver, random),                   \
        __DRIVER_glue(__generic_driver_deps, random),              \
        __DRIVER_rename_get(random, init),                         \
        __DRIVER_rename_get(random, fini),                         \
        #name, deps                                                \
    )

#define __init__ extern "C"
#define __fini__ extern "C"

#define GENERIC_DRIVER(name, init, fini, deps...) __GENERIC_DRIVER_expand(__DRIVER_random(name), name, init, fini, deps)

// ACPI

#define __ACPI_DRIVER_internal(drv_obj_name, pnp_ids_obj_name, _probe, drv_name, _pnp_ids...)                                                                                \
    __DRIVER_list(const char *, pnp_ids_obj_name[], { _pnp_ids })                                                                                                      \
    __DRIVER_internal(drv_name, drv_obj_name, acpi, { .probe = _probe, .num_pnp_ids = __DRIVER_count(pnp_ids_obj_name, const char *), .pnp_ids = pnp_ids_obj_name })

#define __ACPI_DRIVER_expand(random, name, probe, pnp_ids...) \
    __ACPI_DRIVER_internal(                                   \
        __DRIVER_glue(__acpi_driver, random),                 \
        __DRIVER_glue(__acpi_driver_pnp_ids, random),         \
        probe, #name, pnp_ids                                 \
    )

#define ACPI_DRIVER(name, probe, pnp_ids...) __ACPI_DRIVER_expand(__DRIVER_random(name), name, probe, pnp_ids)
