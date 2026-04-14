// Copyright (C) 2024-2026  ilobilo

module system.sched;

namespace sched
{
    namespace
    {
        void set_creds(process_t *process, const std::shared_ptr<cred_t> &new_cred)
        {
            const std::unique_lock _ { process->lock };
            process->cred = new_cred;
        }

        void euid_transition(std::shared_ptr<cred_t> &cred, uid_t old_euid)
        {
            if (old_euid == 0 && cred->euid != 0)
            {
                if ((cred->securebits & secbit_t::no_setuid_fixup) == secbit_t::none)
                    cred->effective = cap_t::none;
            }

            if (old_euid != 0 && cred->euid == 0)
            {
                if ((cred->securebits & secbit_t::no_setuid_fixup) == secbit_t::none)
                    cred->effective = cred->permitted;
            }

            if (cred->ruid != 0 && cred->euid != 0 && cred->suid != 0)
            {
                if ((cred->securebits & secbit_t::keep_caps) != secbit_t::none)
                {
                    cred->permitted = cap_t::none;
                    cred->effective = cap_t::none;
                }
                cred->ambient = cap_t::none;
            }
        }
    } // namespace

    bool capable(const std::shared_ptr<cred_t> &cred, cap_t cap)
    {
        return has_cap(cred->effective, cap);
    }

    bool capable(cap_t cap)
    {
        return capable(current_process()->cred, cap);
    }

    bool check_perms(const std::shared_ptr<cred_t> &cred, const stat &stat, access_mode desired)
    {
        if (capable(cred, cap_t::dac_override))
        {
            if ((desired & access_mode::exec) == access_mode::none)
                return true;

            if (stat.st_mode & (s_ixusr | s_ixgrp | s_ixoth))
                return true;
        }

        if (capable(cred, cap_t::dac_read_search))
        {
            if ((desired & access_mode::write) == access_mode::none &&
                (desired & access_mode::exec) == access_mode::none)
                return true;
        }

        mode_t mode = stat.st_mode;
        mode_t granted = 0;

        const auto has_supgid = [&] {
            return cred->supp_gids.contains(stat.st_gid);
        };

        if (cred->fsuid == stat.st_uid)
            granted = (mode >> 6) & 7;
        else if (cred->fsgid == stat.st_gid || has_supgid())
            granted = (mode >> 3) & 7;
        else
            granted = mode & 7;

        mode_t needed = 0;
        if ((desired & access_mode::read) != access_mode::none)
            needed |= 4;
        if ((desired & access_mode::write) != access_mode::none)
            needed |= 2;
        if ((desired & access_mode::exec) != access_mode::none)
            needed |= 1;

        return (granted & needed) == needed;
    }

    bool check_perms(const stat &stat, access_mode mode)
    {
        return check_perms(current_process()->cred, stat, mode);
    }

    bool check_kill(int sig, const process_t *target)
    {
        const auto &cred = current_process()->cred;
        if (capable(cred, cap_t::kill))
            return true;

        const auto &tcred = target->cred;
        if (cred->euid == tcred->euid || cred->euid == tcred->suid ||
            cred->ruid == tcred->euid || cred->ruid == tcred->suid)
            return true;

        // if (sig == SIGCONT && current_session == target_session)
        //     return true;
        lib::unused(sig);

        return false;
    }

    lib::expect<void> setuid(uid_t uid)
    {
        auto process = current_process();
        const auto &old_cred = process->cred;

        const bool can_setuid = capable(old_cred, cap_t::setuid);
        if (!can_setuid && uid != old_cred->ruid && uid != old_cred->suid)
            return std::unexpected { lib::err::not_permitted };

        auto new_cred = old_cred->clone();
        if (old_cred->euid == 0)
        {
            new_cred->ruid = uid;
            new_cred->suid = uid;
        }
        new_cred->euid = uid;
        new_cred->fsuid = uid;

        euid_transition(new_cred, old_cred->euid);
        set_creds(process, std::move(new_cred));
        return { };
    }

    lib::expect<void> setreuid(uid_t ruid, uid_t euid)
    {
        constexpr auto empty = static_cast<uid_t>(-1);

        auto process = current_process();
        const auto &old_cred = process->cred;

        if (!capable(old_cred, cap_t::setuid))
        {
            if (ruid != empty && ruid != old_cred->ruid && ruid != old_cred->euid)
                return std::unexpected { lib::err::not_permitted };

            if (euid != empty && euid != old_cred->ruid &&
                euid != old_cred->euid && euid != old_cred->suid)
                return std::unexpected { lib::err::not_permitted };
        }

        // TODO: other failure cases?

        auto new_cred = old_cred->clone();
        if (ruid != empty)
            new_cred->ruid = ruid;
        if (euid != empty)
            new_cred->euid = euid;
        new_cred->fsuid = euid;

        if (ruid != empty || (euid != empty && euid != old_cred->euid))
            new_cred->suid = new_cred->euid;

        euid_transition(new_cred, old_cred->euid);
        set_creds(process, std::move(new_cred));
        return { };
    }

    lib::expect<void> setresuid(uid_t ruid, uid_t euid, uid_t suid)
    {
        constexpr auto empty = static_cast<uid_t>(-1);

        auto process = current_process();
        const auto &old_cred = process->cred;

        if ((ruid == empty || ruid == old_cred->ruid) &&
            (euid == empty || (euid == old_cred->euid && euid == old_cred->fsuid)) &&
            (suid == empty || suid == old_cred->suid))
            return { };

        if (!capable(old_cred, cap_t::setuid))
        {
            const auto can_set = [&](uid_t val) {
                return val == empty ||
                       val == old_cred->ruid ||
                       val == old_cred->euid ||
                       val == old_cred->suid;
            };

            if (!can_set(ruid) || !can_set(euid) || !can_set(suid))
                return std::unexpected { lib::err::not_permitted };
        }

        auto new_cred = old_cred->clone();
        if (ruid != empty)
            new_cred->ruid = ruid;
        if (euid != empty)
            new_cred->euid = euid;
        if (suid != empty)
            new_cred->suid = suid;
        new_cred->fsuid = euid;

        euid_transition(new_cred, old_cred->euid);
        set_creds(process, std::move(new_cred));
        return { };
    }

    lib::expect<void> setgid(gid_t gid)
    {
        auto process = current_process();
        const auto &old_cred = process->cred;

        const bool can_setgid = capable(old_cred, cap_t::setgid);
        if (!can_setgid && gid != old_cred->rgid && gid != old_cred->sgid)
            return std::unexpected { lib::err::not_permitted };

        auto new_cred = old_cred->clone();
        if (can_setgid)
        {
            new_cred->rgid = gid;
            new_cred->sgid = gid;
        }
        new_cred->egid = gid;

        set_creds(process, std::move(new_cred));
        return { };
    }

    lib::expect<void> setregid(gid_t rgid, gid_t egid)
    {
        constexpr auto empty = static_cast<uid_t>(-1);

        auto process = current_process();
        const auto &old_cred = process->cred;

        const bool can_setgid = capable(old_cred, cap_t::setgid);

        if (!can_setgid)
        {
            if (rgid != empty && rgid != old_cred->rgid && rgid != old_cred->egid)
                return std::unexpected { lib::err::not_permitted };

            if (egid != empty && egid != old_cred->rgid &&
                egid != old_cred->egid && egid != old_cred->sgid)
                return std::unexpected { lib::err::not_permitted };
        }

        // TODO: other failure cases?

        auto new_cred = old_cred->clone();
        if (rgid != empty)
            new_cred->rgid = rgid;
        if (egid != empty)
            new_cred->egid = egid;
        new_cred->fsgid = egid;

        if (rgid != empty || (egid != empty && egid != old_cred->egid))
            new_cred->sgid = new_cred->egid;

        set_creds(process, std::move(new_cred));
        return { };
    }

    lib::expect<void> setresgid(gid_t rgid, gid_t egid, gid_t sgid)
    {
        constexpr auto empty = static_cast<uid_t>(-1);

        auto process = current_process();
        const auto &old_cred = process->cred;

        if ((rgid == empty || rgid == old_cred->rgid) &&
            (egid == empty || (egid == old_cred->egid && egid == old_cred->fsgid)) &&
            (sgid == empty || sgid == old_cred->sgid))
            return { };

        if (!capable(old_cred, cap_t::setgid))
        {
            const auto can_set = [&](gid_t val) {
                return val == empty ||
                       val == old_cred->rgid ||
                       val == old_cred->egid ||
                       val == old_cred->sgid;
            };

            if (!can_set(rgid) || !can_set(egid) || !can_set(sgid))
                return std::unexpected { lib::err::not_permitted };
        }

        auto new_cred = old_cred->clone();
        if (rgid != empty)
            new_cred->rgid = rgid;
        if (egid != empty)
            new_cred->egid = egid;
        if (sgid != empty)
            new_cred->sgid = sgid;
        new_cred->fsgid = egid;

        set_creds(process, std::move(new_cred));
        return { };
    }

    lib::expect<void> setgroups(lib::maybe_uspan<gid_t> groups)
    {
        auto process = current_process();
        if (!capable(process->cred, cap_t::setgid))
            return std::unexpected { lib::err::not_permitted };

        auto new_cred = process->cred->clone();

        new_cred->supp_gids.gids.resize(groups.size());
        if (!groups.copy_to(new_cred->supp_gids.gids))
            return std::unexpected { lib::err::invalid_address };
        std::ranges::sort(new_cred->supp_gids.gids);

        set_creds(process, std::move(new_cred));
        return { };
    }

    lib::expect<std::size_t> getgroups(lib::maybe_uspan<gid_t> groups)
    {
        auto process = current_process();
        const auto &cred = process->cred;
        if (cred->supp_gids.gids.empty())
            return 0;

        auto &gids = cred->supp_gids.gids;
        const auto size = std::min(groups.size(), gids.size());

        std::span<gid_t> span { gids.data(), size };
        if (!groups.copy_from(span))
            return std::unexpected { lib::err::invalid_address };

        return size;
    }

    lib::expect<void> capget(pid_t pid, cap_user_data_t *data)
    {
        lib::bug_on(!data);
        std::shared_ptr<cred_t> cred;
        if (pid != 0)
        {
            auto proc = get_process(pid);
            if (!proc)
                return std::unexpected { lib::err::not_found };
            cred = proc->cred;
        }
        else cred = current_process()->cred;

        data->effective = cred->effective;
        data->permitted = cred->permitted;
        data->inheritable = cred->inheritable;
        return { };
    }

    lib::expect<void> capset(pid_t pid, const cap_user_data_t *data)
    {
        lib::bug_on(!data);
        auto process = current_process();
        if (pid != 0 && pid != process->pid)
            return std::unexpected { lib::err::not_permitted };

        const auto &old_cred = process->cred;
        if ((data->permitted & ~old_cred->permitted) != cap_t::none)
            return std::unexpected { lib::err::not_permitted };

        if ((data->effective & ~data->permitted) != cap_t::none)
            return std::unexpected { lib::err::not_permitted };

        if (!capable(old_cred, cap_t::setpcap))
        {
            const auto allowed = old_cred->inheritable | old_cred->permitted;
            if ((data->inheritable & ~allowed) != cap_t::none)
                return std::unexpected { lib::err::not_permitted };
        }

        auto new_cred = old_cred->clone();
        new_cred->effective = data->effective;
        new_cred->permitted = data->permitted;
        new_cred->inheritable = data->inheritable;
        new_cred->ambient &= new_cred->permitted & new_cred->inheritable;

        set_creds(process, std::move(new_cred));
        return { };
    }

    lib::expect<void> cap_bounding_drop(cap_t cap)
    {
        auto process = current_process();
        const auto &old_cred = process->cred;
        if (!capable(old_cred, cap_t::setpcap))
            return std::unexpected { lib::err::not_permitted };

        auto new_cred = old_cred->clone();
        new_cred->bounding &= ~cap;
        set_creds(process, std::move(new_cred));
        return { };
    }

    lib::expect<void> cap_ambient_raise(cap_t cap)
    {
        auto process = current_process();
        const auto &old_cred = process->cred;

        if ((old_cred->securebits & secbit_t::no_cap_ambient_raise) != secbit_t::none)
            return std::unexpected { lib::err::not_permitted };

        if (!has_cap(old_cred->permitted, cap) ||
            !has_cap(old_cred->inheritable, cap))
            return std::unexpected { lib::err::not_permitted };

        auto new_cred = old_cred->clone();
        new_cred->ambient |= cap;
        set_creds(process, std::move(new_cred));
        return { };
    }

    void cap_ambient_lower(cap_t cap)
    {
        auto process = current_process();
        const auto &old_cred = process->cred;
        auto new_cred = old_cred->clone();
        new_cred->ambient &= ~cap;
        set_creds(process, std::move(new_cred));
    }

    lib::expect<void> set_securebits(secbit_t securebits)
    {
        auto process = current_process();
        const auto &old_cred = process->cred;

        const auto old_bits = old_cred->securebits;

        const auto old_locked = old_bits & secbit_t::all_locked;
        const auto new_locked = securebits & secbit_t::all_locked;
        if ((old_locked & ~new_locked) != secbit_t::none)
            return std::unexpected { lib::err::not_permitted };

        const auto check_locked = [&](secbit_t bit, secbit_t lock) {
            return !((old_bits & lock) != secbit_t::none &&
                    ((old_bits & bit) != (securebits & bit)));
        };

        if (!check_locked(secbit_t::noroot, secbit_t::noroot_locked) ||
            !check_locked(secbit_t::no_setuid_fixup, secbit_t::no_setuid_fixup_locked) ||
            !check_locked(secbit_t::keep_caps, secbit_t::keep_caps_locked) ||
            !check_locked(secbit_t::no_cap_ambient_raise, secbit_t::no_cap_ambient_raise_locked) ||
            !check_locked(secbit_t::exec_restrict_file, secbit_t::exec_restrict_file_locked) ||
            !check_locked(secbit_t::exec_deny_interactive, secbit_t::exec_deny_interactive_locked))
            return std::unexpected { lib::err::not_permitted };

        if ((securebits & secbit_t::exec_restrict_file) != secbit_t::none &&
            (securebits & secbit_t::exec_restrict_file_locked) == secbit_t::none)
            return std::unexpected { lib::err::invalid_argument };

        if ((securebits & secbit_t::exec_deny_interactive) != secbit_t::none &&
            (securebits & secbit_t::exec_deny_interactive_locked) == secbit_t::none)
            return std::unexpected { lib::err::invalid_argument };

        auto new_cred = old_cred->clone();
        new_cred->securebits = securebits;
        set_creds(process, std::move(new_cred));
        return { };
    }
} // namespace sched
