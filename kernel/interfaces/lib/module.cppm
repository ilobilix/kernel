// Copyright (C) 2024-2026  ilobilo

module;

#include <version.h>

export module lib:mod;

import :string;
import fmt;
import std;

export namespace mod
{
    enum class type : std::uint32_t
    {
        generic,
        filesystem,
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

        consteval deps() requires (NDeps == 0) : list { } { }

        template<typename ...Args>
            requires (!(... && std::same_as<std::decay_t<Args>, deps>))
        consteval deps(Args &&...deps) : list { deps... } { }
    };

    template<typename ...Args>
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
    consteval match_bytes<sizeof(Id) * Num> ids_match_impl(std::span<const Id, Num> ids)
    {
        match_bytes<sizeof(Id) * Num> out { Num, sizeof(Id), { } };
        for (std::size_t i = 0; i < Num; i++)
        {
            const auto raw = std::bit_cast<std::array<std::byte, sizeof(Id)>>(ids[i]);
            std::copy_n(raw.begin(), raw.size(), out.data.begin() + i * sizeof(Id));
        }
        return out;
    }

    template<typename Id, std::size_t Num>
    consteval auto ids_match(const Id (&ids)[Num])
    {
        return ids_match_impl<Id, Num>(ids);
    }

    template<typename Id, std::size_t Num>
    consteval auto ids_match(const std::array<Id, Num> &ids)
    {
        return ids_match_impl<Id, Num>(ids);
    }

    template<lib::comptime_string Str>
    consteval match_bytes<Str.size()> string_match()
    {
        match_bytes<Str.size()> out { 1, Str.size(), { } };
        for (std::size_t i = 0; i < Str.size(); i++)
            out.data[i] = std::bit_cast<std::byte>(Str.value[i]);
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

        std::string_view name() const
        {
            return lib::trim(_name);
        }

        auto dependencies() const
        {
            return std::span<const char *const> { _deps.list, _deps.ndeps } |
                std::views::transform(lib::trim);
        }

        std::span<const std::byte> matches() const
        {
            const auto bytes = reinterpret_cast<const std::byte *>(&_deps) +
                sizeof(_deps.ndeps) + _deps.ndeps * sizeof(const char *);
            return { bytes, static_cast<std::size_t>(match_count) * match_stride };
        }

        consteval declare(
            const char *name, const char *description, enum type type,
            bool (*init)(), bool (*fini)(),
            const match_bytes<MatchBytes> &_match, deps<NDeps> deps = { }
        ) : _name { name }, description { description },
            init { init }, fini { fini }, type { type },
            match_count { _match.count }, match_stride { _match.stride },
            _deps { deps }, match { }
        {
            std::copy_n(_match.data.begin(), _match.data.size(), match);
        }
    };

    template<std::size_t Matches, std::size_t Deps>
    declare(
        const char *, const char *, type, bool (*)(), bool (*)(),
        match_bytes<Matches>, deps<Deps>
    ) -> declare<Deps, Matches>;

    template<std::size_t Matches>
    declare(
        const char *, const char *, type, bool (*)(), bool (*)(),
        match_bytes<Matches>
    ) -> declare<0, Matches>;

    template<auto Id>
    struct modalias_generator;

    template<const auto &Ids>
    consteval auto get_modaliases_array()
    {
        return []<std::size_t ...Is>(std::index_sequence<Is...>) {
            constexpr std::tuple strings { modalias_generator<Ids[Is]>::get()... };
            constexpr auto max_len = std::max({ 0zu, (std::get<Is>(strings).size() + 1)... });

            const auto pad_to_max = [&](const auto &str) {
                std::array<char, max_len> padded { };
                std::copy_n(str.value, str.size() + 1, padded.begin());
                return padded;
            };

            return std::array<std::array<char, max_len>, sizeof...(Is)> {
                pad_to_max(std::get<Is>(strings))...
            };
        } (std::make_index_sequence<std::size(Ids)> { });
    }

    template<lib::comptime_string Prefix, const auto &Ids>
    consteval auto get_modaliases()
    {
        return []<std::size_t ...Is>(std::index_sequence<Is...>) {
            return std::tuple {
                lib::consteval_format<
                    "{}alias={}"_cf, Prefix,
                    modalias_generator<Ids[Is]>::get()
                >()...
            };
        } (std::make_index_sequence<std::size(Ids)> { });
    }

    template<lib::comptime_string Prefix, std::size_t ...Len>
    consteval auto format_depends(const char (&...deps)[Len])
    {
        constexpr auto prefix = lib::consteval_format<"{}softdep=pre: "_cf, Prefix.value>();

        constexpr auto valid_deps = (0uz + ... + (Len > 1));
        constexpr auto deps_len = (0uz + ... + (Len > 1 ? Len - 1 : 0));
        constexpr auto spaces_len = valid_deps > 0 ? valid_deps - 1 : 0uz;

        constexpr auto total_len = prefix.size() + deps_len + spaces_len + 1;

        std::array<char, total_len> buffer { };
        auto out = std::copy_n(prefix.data(), prefix.size(), buffer.begin());

        bool first = true;
        ([&](const char *str, std::size_t len) {
            if (len <= 1)
                return;

            if (!first)
                *out++ = ' ';

            out = std::copy_n(str, len - 1, out);
            first = false;
        } (deps, Len), ...);

        return lib::comptime_string { buffer };
    }
} // export namespace mod
