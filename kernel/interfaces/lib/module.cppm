// Copyright (C) 2024-2025  ilobilo

export module lib:mod;
import std;

export namespace mod
{
    struct [[gnu::packed]] generic
    {
        bool (*init)();
        bool (*fini)();
    };

    struct [[gnu::packed]] pci
    {
    };

    struct [[gnu::packed]] acpi
    {
    };

    template<std::size_t NDeps>
    struct [[gnu::packed]] deps
    {
        static inline constexpr std::size_t count = NDeps;
        const char *deps[NDeps];
    };

    template<std::size_t NDeps>
    struct [[gnu::packed]] declare
    {
        const std::uint64_t magic_start = 0x737BDF086B7EF53C;

        const char *name;
        const char *description;
        const std::variant<
            generic,
            pci,
            acpi
        > interface;
        const deps<NDeps> dependencies;

        const std::uint64_t magic_end = 0x27C704CC79A3CED1;

        declare(const char *name, const char *description, auto interface, auto &&...args)
            : name { name }, description { description }, interface { interface }, dependencies { args... } { }
    };

    template<typename ...Args>
    deps(Args &&...) -> deps<sizeof...(Args)>;

    template<typename Type, typename Deps>
    declare(const char *, const char *, Type, Deps) -> declare<Deps::count>;
} // export namespace mod

// declare_module(nvme_module) {
//     "nvme", "NVMe disk driver",
//     mod::generic {
//         init, fini
//     },
//     mod::deps {
//         "pci", "acpi", "test"
//     }
// };