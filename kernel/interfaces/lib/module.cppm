// Copyright (C) 2024-2026  ilobilo

module;

#include <version.h>

export module lib:mod;
import std;

export namespace mod
{
    enum class type
    {
        generic,
        pci,
        acpi
    };

    template<std::size_t NDeps>
    struct deps
    {
        static constexpr std::size_t count = NDeps;
        const std::size_t ndeps = NDeps;
        const char *const list[NDeps];

        consteval deps(const deps &) = default;

        consteval deps()
            requires(NDeps == 0)
            : list { }
        { }

        template<typename... Args>
            requires(!(... && std::same_as<std::decay_t<Args>, deps>))
        consteval deps(Args &&...deps) : list { deps... }
        { }
    };

    template<typename... Args>
    deps(Args &&...) -> deps<sizeof...(Args)>;

    template<std::size_t Bytes>
    struct match_bytes
    {
        std::uint16_t count;
        std::uint16_t stride;
        std::array<std::byte, Bytes> data;
    };

    constexpr match_bytes<0> no_match { 0, 0, { } };

    template<typename Id, std::size_t Num>
        requires std::has_unique_object_representations_v<Id>
    consteval match_bytes<sizeof(Id) * Num> inline_match(const Id (&ids)[Num])
    {
        match_bytes<sizeof(Id) * Num> out { Num, sizeof(Id), { } };
        for (std::size_t i = 0; i < Num; i++)
        {
            const auto raw = std::bit_cast<std::array<std::byte, sizeof(Id)>>(ids[i]);
            std::copy_n(raw.begin(), raw.size(), out.data.begin() + i * sizeof(Id));
        }
        return out;
    }

    template<std::size_t NDeps, std::size_t MatchBytes>
    struct declare
    {
        static constexpr std::uint64_t header_magic = 0x737BDF086B7EF53C;
        static constexpr std::string_view build_version { ILOBILIX_RELEASE };

        const std::uint64_t magic = header_magic;
        const std::uint64_t struct_size = sizeof(declare);
        const char *const version = build_version.data();

        const char *const _name;
        const char *const description;

        bool (*init)();
        bool (*fini)();
        type type;

        std::uint16_t match_count;
        std::uint16_t match_stride;

        const deps<NDeps> _deps;
        alignas(8) std::byte match[MatchBytes ?: 1];

        std::string_view name() const { return _name; }

        std::span<const char *const> dependencies() const { return { _deps.list, _deps.ndeps }; }

        std::span<const std::byte> matches() const
        {
            const auto bytes = reinterpret_cast<const std::byte *>(&_deps) + sizeof(_deps.ndeps) +
                _deps.ndeps * sizeof(const char *);
            return { bytes, static_cast<std::size_t>(match_count) * match_stride };
        }

        consteval declare(
            const char *name, const char *description, enum type type, bool (*init)(),
            bool (*fini)(), const match_bytes<MatchBytes> &m, deps<NDeps> deps = { }
        )
            : _name { name }, description { description }, init { init }, fini { fini }, type { type },
              match_count { m.count }, match_stride { m.stride }, _deps { deps }, match { }
        {
            std::copy_n(m.data.begin(), m.data.size(), match);
        }
    };

    template<std::size_t Matches, std::size_t Deps>
    declare(
        const char *, const char *, type, bool (*)(), bool (*)(), match_bytes<Matches>, deps<Deps>
    ) -> declare<Deps, Matches>;

    template<std::size_t Matches>
    declare(const char *, const char *, type, bool (*)(), bool (*)(), match_bytes<Matches>)
        -> declare<0, Matches>;
} // export namespace mod
