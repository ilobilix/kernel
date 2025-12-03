// Copyright (C) 2024-2025  ilobilo

export module lib:mod;
import std;

export namespace mod
{
    struct generic
    {
        bool (*init)();
        bool (*fini)();
    };

    struct pci
    {
    };

    struct acpi
    {
    };

    template<std::size_t NDeps>
    struct deps
    {
        static inline constexpr std::size_t count = NDeps;
        const std::size_t ndeps = NDeps;
        const char *list[NDeps];

        consteval deps(const deps &) = default;

        consteval deps() requires (NDeps == 0) : list { } { }

        template<typename ...Args> requires (!(... && std::same_as<std::decay_t<Args>, deps>))
        consteval deps(Args &&...deps) : list { deps... } { }
    };

    template<std::size_t NDeps>
    struct declare
    {
        static inline constexpr std::uint64_t header_magic = 0x737BDF086B7EF53C;
        const std::uint64_t magic = header_magic;

        const char *name;
        const char *description;
        const std::variant<
            generic,
            pci,
            acpi
        > interface;
        const deps<NDeps> dependencies;

        consteval declare(const char *name, const char *description, auto interface, auto &&...args)
            : name { name }, description { description }, interface { interface }, dependencies { args... } { }
    };

    template<typename ...Args>
    deps(Args &&...) -> deps<sizeof...(Args)>;

    template<typename Type, typename Deps>
    declare(const char *, const char *, Type, Deps) -> declare<Deps::count>;

    template<typename Type>
    declare(const char *, const char *, Type) -> declare<0>;
} // export namespace mod