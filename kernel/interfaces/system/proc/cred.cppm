// Copyright (C) 2024-2026  ilobilo

export module system.sched.cred;

import magic_enum;
import lib;
import std;

export namespace sched
{
    enum class secure_t
    {
        noroot = 0,
        noroot_locked = 1,
        no_setuid_fixup = 2,
        no_setuid_fixup_locked = 3,
        keep_caps = 4,
        keep_caps_locked = 5,
        no_cap_ambient_raise = 6,
        no_cap_ambient_raise_locked = 7,
        exec_restrict_file = 8,
        exec_restrict_file_locked = 9,
        exec_deny_interactive = 10,
        exec_deny_interactive_locked = 11
    };

    inline constexpr int issecure_mask(secure_t sec)
    {
        return (1 << static_cast<std::size_t>(sec));
    }

    enum class secbit_t
    {
        none = 0,
        noroot = issecure_mask(secure_t::noroot),
        noroot_locked = issecure_mask(secure_t::noroot_locked),
        no_setuid_fixup = issecure_mask(secure_t::no_setuid_fixup),
        no_setuid_fixup_locked = issecure_mask(secure_t::no_setuid_fixup_locked),
        keep_caps = issecure_mask(secure_t::keep_caps),
        keep_caps_locked = issecure_mask(secure_t::keep_caps_locked),
        no_cap_ambient_raise = issecure_mask(secure_t::no_cap_ambient_raise),
        no_cap_ambient_raise_locked = issecure_mask(secure_t::no_cap_ambient_raise_locked),
        exec_restrict_file = issecure_mask(secure_t::exec_restrict_file),
        exec_restrict_file_locked = issecure_mask(secure_t::exec_restrict_file_locked),
        exec_deny_interactive = issecure_mask(secure_t::exec_deny_interactive),
        exec_deny_interactive_locked = issecure_mask(secure_t::exec_deny_interactive_locked),
        all =
            (noroot | no_setuid_fixup | keep_caps | no_cap_ambient_raise | exec_restrict_file |
             exec_deny_interactive),
        all_locked = (all << 1),
        all_unprivileged = (exec_restrict_file | exec_deny_interactive)
    };

    enum class cap_t : std::uint64_t
    {
        none = 0,
        chown = (1ul << 0),
        dac_override = (1ul << 1),
        dac_read_search = (1ul << 2),
        fowner = (1ul << 3),
        fsetid = (1ul << 4),
        kill = (1ul << 5),
        setgid = (1ul << 6),
        setuid = (1ul << 7),
        setpcap = (1ul << 8),
        linux_immutable = (1ul << 9),
        net_bind_service = (1ul << 10),
        net_broadcast = (1ul << 11),
        net_admin = (1ul << 12),
        net_raw = (1ul << 13),
        ipc_lock = (1ul << 14),
        ipc_owner = (1ul << 15),
        sys_module = (1ul << 16),
        sys_rawio = (1ul << 17),
        sys_chroot = (1ul << 18),
        sys_ptrace = (1ul << 19),
        sys_pacct = (1ul << 20),
        sys_admin = (1ul << 21),
        sys_boot = (1ul << 22),
        sys_nice = (1ul << 23),
        sys_resource = (1ul << 24),
        sys_time = (1ul << 25),
        sys_tty_config = (1ul << 26),
        mknod = (1ul << 27),
        lease = (1ul << 28),
        audit_write = (1ul << 29),
        audit_control = (1ul << 30),
        setfcap = (1ul << 31),
        mac_override = (1ul << 32),
        mac_admin = (1ul << 33),
        syslog = (1ul << 34),
        wake_alarm = (1ul << 35),
        block_suspend = (1ul << 36),
        audit_read = (1ul << 37),
        perfmon = (1ul << 38),
        bpf = (1ul << 39),
        checkpoint_restore = (1ul << 40),
        all = ~0ul
    };

    using namespace magic_enum::bitwise_operators;

    inline constexpr bool cap_valid(cap_t cap) { return magic_enum::enum_flags_contains(cap); }

    inline constexpr bool secbit_valid(secbit_t bits)
    {
        return magic_enum::enum_flags_contains(bits);
    }

    inline constexpr bool has_cap(cap_t set, cap_t cap) { return (set & cap) == cap; }

    inline constexpr bool has_secbit(secbit_t bits, secbit_t flag) { return (bits & flag) == flag; }

    static_assert(!cap_valid(static_cast<cap_t>(1ul << 41)));
    static_assert(!secbit_valid(static_cast<secbit_t>(1ul << 33)));

    // supplementary gids
    struct groups_t
    {
        std::vector<gid_t> gids;

        bool contains(gid_t gid) const { return std::ranges::contains(gids, gid); }
    };

    struct cred_t
    {
        uid_t ruid = 0; // real
        uid_t euid = 0; // effective
        uid_t suid = 0; // saved
        uid_t fsuid = 0; // filesystem

        gid_t rgid = 0;
        gid_t egid = 0;
        gid_t sgid = 0;
        gid_t fsgid = 0;

        groups_t supp_gids;

        // upper bound for effective
        cap_t permitted = cap_t::none;
        // preserved across execve
        cap_t inheritable = cap_t::none;
        // currently in effect
        cap_t effective = cap_t::none;
        // upper bound for execve
        cap_t bounding = cap_t::all;
        // added to effective on execve if permitted and inheritable
        cap_t ambient = cap_t::none;

        secbit_t securebits = secbit_t::none;

        static std::shared_ptr<cred_t> root()
        {
            auto cred = std::make_shared<cred_t>();
            cred->permitted = cap_t::all;
            cred->inheritable = cap_t::all;
            cred->effective = cap_t::all;
            cred->bounding = cap_t::all;
            cred->ambient = cap_t::all;
            return cred;
        }

        static std::shared_ptr<cred_t> user(uid_t uid, gid_t gid)
        {
            auto cred = std::make_shared<cred_t>();
            cred->ruid = cred->euid = cred->suid = cred->fsuid = uid;
            cred->rgid = cred->egid = cred->sgid = cred->fsgid = gid;
            return cred;
        }

        std::shared_ptr<cred_t> clone() const { return std::make_shared<cred_t>(*this); }
    };
} // export namespace sched
