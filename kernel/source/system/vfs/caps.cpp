// Copyright (C) 2024-2026  ilobilo

module system.vfs.caps;

namespace vfs
{
    std::optional<file_caps> parse_file_caps(std::span<const std::byte> data)
    {
        constexpr std::size_t rev3_len = sizeof(cap_data);
        constexpr std::size_t rev2_len = rev3_len - sizeof(cap_data::rootid);

        if (data.size() < rev2_len || data.size() > rev3_len)
            return std::nullopt;

        bool is_rev3 = data.size() == rev3_len;

        cap_data raw;
        std::memcpy(&raw, data.data(), is_rev3 ? rev3_len : rev2_len);

        const auto rev = raw.magic & cap_revision_mask;
        if (rev != cap_revision_2 && rev != cap_revision_3)
            return std::nullopt;

        is_rev3 = (is_rev3 && rev == cap_revision_3);

        return file_caps {
            .permitted = static_cast<sched::cap_t>(
                static_cast<std::uint64_t>(raw.data[0].permitted) |
                (static_cast<std::uint64_t>(raw.data[1].permitted) << 32)
            ),
            .inheritable = static_cast<sched::cap_t>(
                static_cast<std::uint64_t>(raw.data[0].inheritable) |
                (static_cast<std::uint64_t>(raw.data[1].inheritable) << 32)
            ),
            .effective = (raw.magic & cap_flags_effective) != 0,
            .rootid = is_rev3 ? raw.rootid : 0
        };
    }
} // namespace vfs
