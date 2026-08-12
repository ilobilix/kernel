// Copyright (C) 2024-2026  ilobilo

module system.input;

import fmt;

namespace input
{
    namespace
    {
        std::atomic_size_t next_index = 0;

        sched::mutex_t registry_lock;
        std::vector<consumer_t *> consumers;
        std::vector<std::weak_ptr<device_t>> devices;

        std::vector<std::shared_ptr<device_t>> locked_devices()
        {
            std::vector<std::shared_ptr<device_t>> ret;
            lib::erase_if(devices, [&ret](const std::weak_ptr<device_t> &weak) {
                auto dev = weak.lock();
                if (!dev)
                    return true;

                ret.push_back(std::move(dev));
                return false;
            });
            return ret;
        }

        lib::expect<std::uint32_t> keymap_index(const keymap_entry_t &entry)
        {
            if (entry.flags & keymap_by_index)
                return entry.index;

            if (entry.len == 1 || entry.len == 2 || entry.len == 4)
            {
                std::uint32_t scancode = 0;
                std::memcpy(&scancode, entry.scancode, entry.len);
                return scancode;
            }
            return std::unexpected { lib::err::invalid_argument };
        }

        std::int32_t defuzz(std::int32_t value, std::int32_t old, std::int32_t fuzz)
        {
            if (fuzz)
            {
                if (value > old - fuzz / 2 && value < old + fuzz / 2)
                    return old;
                if (value > old - fuzz && value < old + fuzz)
                    return (old * 3 + value) / 4;
                if (value > old - fuzz * 2 && value < old + fuzz * 2)
                    return (old + value) / 2;
            }
            return value;
        }

        std::string print_bitmap(const lib::const_bitmap_view bits)
        {
            const auto bytes = bits.size_bytes();
            const auto words = lib::div_roundup(bytes, 8u);

            std::string ret;
            for (std::size_t i = words; i-- > 0; )
            {
                std::uint64_t word = 0;
                for (std::size_t b = 0; b < 8; b++)
                {
                    if (const auto idx = i * 8 + b; idx < bytes)
                        word |= static_cast<std::uint64_t>(bits.data()[idx]) << (b * 8);
                }

                if (ret.empty() && word == 0 && i != 0)
                    continue;

                if (!ret.empty())
                    ret += ' ';
                fmt::format_to(std::back_inserter(ret), "{:x}", word);
            }
            return ret + "\n";
        }

        struct input_ktype_t final : dev::ktype_t
        {
            std::span<dev::attribute_t *const> attributes() const override
            {
                struct string_attribute_t : dev::make_attribute_t
                {
                    string_attribute_t(std::string device_t::*member, std::string_view name)
                        : dev::make_attribute_t {
                            [member](dev::device_t &dev) -> lib::expect<std::string> {
                                return fmt::format("{}\n", static_cast<device_t &>(dev).*member);
                            }, nullptr, name, 0444
                        } { }
                };

                static string_attribute_t name { &device_t::desc, "name" };
                static string_attribute_t phys { &device_t::phys, "phys" };
                static string_attribute_t uniq { &device_t::uniq, "uniq" };

                static dev::make_attribute_t properties {
                    [](dev::device_t &dev) -> lib::expect<std::string> {
                        return print_bitmap(static_cast<device_t &>(dev).props);
                    }, nullptr, "properties", 0444
                };

                static dev::make_attribute_t modalias {
                    [](dev::device_t &dev) -> lib::expect<std::string> {
                        return fmt::format("{}\n", dev.modalias);
                    }, nullptr, "modalias", 0444
                };

                static dev::attribute_t *list[] {
                    &name,
                    &phys,
                    &uniq,
                    &properties,
                    &modalias
                };
                return list;
            }

            std::span<const dev::attribute_group_t> groups() const override
            {
                struct cap_attribute_t : dev::make_attribute_t
                {
                    cap_attribute_t(std::uint16_t type, std::string_view name)
                        : dev::make_attribute_t {
                            [type](dev::device_t &dev) -> lib::expect<std::string> {
                                return print_bitmap(static_cast<device_t &>(dev).supported.bits(type)
                                );
                            }, nullptr, name, 0444
                        } { }
                };

                struct id_attribute_t : dev::make_attribute_t
                {
                    id_attribute_t(std::uint16_t id_t::*member, std::string_view name)
                        : dev::make_attribute_t {
                            [member](dev::device_t &dev) -> lib::expect<std::string> {
                                return fmt::format(
                                    "{:04x}\n", static_cast<device_t &>(dev).ident.*member
                                );
                            }, nullptr, name, 0444
                        } { }
                };

                static dev::make_attribute_t ev {
                    [](dev::device_t &dev) -> lib::expect<std::string> {
                        return print_bitmap(static_cast<device_t &>(dev).events);
                    }, nullptr, "ev", 0444
                };

                static cap_attribute_t key { ev_key, "key" };
                static cap_attribute_t rel { ev_rel, "rel" };
                static cap_attribute_t abs { ev_abs, "abs" };
                static cap_attribute_t msc { ev_msc, "msc" };
                static cap_attribute_t led { ev_led, "led" };
                static cap_attribute_t snd { ev_snd, "snd" };
                static cap_attribute_t ff { ev_ff, "ff" };
                static cap_attribute_t sw { ev_sw, "sw" };

                static dev::attribute_t *caps[] {
                    &ev, &key, &rel, &abs, &msc, &led, &snd, &ff, &sw
                };

                static id_attribute_t bustype { &id_t::bustype, "bustype" };
                static id_attribute_t vendor { &id_t::vendor, "vendor" };
                static id_attribute_t product { &id_t::product, "product" };
                static id_attribute_t version { &id_t::version, "version" };

                static dev::attribute_t *ids[] {
                    &bustype,
                    &vendor,
                    &product,
                    &version
                };

                static const dev::attribute_group_t list[] {
                    { "capabilities", caps },
                    { "id", ids }
                };
                return list;
            }
        };


        struct input_class_t final : dev::class_t
        {
            input_class_t() : dev::class_t { "input", dev::empty_ktype(), false } { }

            std::string devnode(const dev::device_t &dev, mode_t &mode) const override
            {
                mode = (mode & ~0777) | 0640;
                return lib::path { "input" } / dev.name;
            }
        };
    } // namespace

    dev::ktype_t &get_ktype()
    {
        static input_ktype_t type { };
        return type;
    }

    dev::class_t &get_class()
    {
        static input_class_t klass { };
        return klass;
    }

    device_t::device_t(std::string_view name, std::weak_ptr<dev::kobject_t> parent)
        : dev::device_t { name, get_ktype(), parent },
          _lock { }, _event { }, _handles { }, _grab { }, _registered { false },
          _inhibited { false }, _open_files { 0 }, _index { 0 }, _ev_per_packet { 0 },
          desc { }, phys { }, uniq { }, ident { }, props { }, events { }, supported { }, repeat { },
          on_event { }, on_open { }, on_close { }, on_getkeycode { }, on_setkeycode { }
    {
        cls = std::addressof(get_class());
        events.set(ev_syn, true);
    }

    lib::expect<std::shared_ptr<device_t>> device_t::create(std::weak_ptr<dev::kobject_t> parent)
    {
        const auto index = next_index.fetch_add(1, std::memory_order_relaxed);
        auto ret = std::make_shared<device_t>(
            lib::private_t<device_t> { },
            fmt::format("input{}", index), parent
        );
        ret->_index = index;
        return ret;
    }

    void device_t::alloc_absinfo()
    {
        if (_event.lock()->absinfo)
            return;

        auto absinfo = std::make_unique<std::array<absinfo_t, abs_cnt>>();
        {
            auto event = _event.lock();
            if (!event->absinfo)
                event->absinfo = std::move(absinfo);
        }
    }

    void device_t::set_cap(std::uint16_t type, std::uint16_t code)
    {
        if (type >= ev_cnt)
            return;

        if (type == ev_abs)
            alloc_absinfo();

        events.set(type, true);
        supported.set(type, code);
    }

    void device_t::set_abs(std::uint16_t axis, const absinfo_t &info)
    {
        if (axis >= abs_cnt)
            return;

        set_cap(ev_abs, axis);

        auto event = _event.lock();
        if (event->absinfo)
            (*event->absinfo)[axis] = info;
    }

    void device_t::set_keymap(std::span<const std::uint32_t> keycodes)
    {
        std::vector<std::uint32_t> copy;
        {
            copy.resize(keycodes.size());
            std::memcpy(copy.data(), keycodes.data(), keycodes.size_bytes());

            auto event = _event.lock();
            event->keycodes = std::move(copy);
        }

        for (const auto code : keycodes)
        {
            if (code != key_reserved && code <= key_max)
                set_cap(ev_key, code);
        }
    }

    lib::expect<void> device_t::set_absinfo(std::uint16_t axis, const absinfo_t &info)
    {
        if (axis >= abs_cnt || !events.get(ev_abs) || !supported.get(ev_abs, axis))
            return std::unexpected { lib::err::invalid_argument };

        auto event = _event.lock();
        if (!event->absinfo)
            return std::unexpected { lib::err::invalid_argument };

        (*event->absinfo)[axis] = info;
        return { };
    }

    std::optional<absinfo_t> device_t::get_abs_locked(
        const locked_event_t &event, std::uint16_t axis
    ) const
    {
        if (axis >= abs_cnt)
            return std::nullopt;
        if (!event->absinfo)
            return std::nullopt;
        return (*event->absinfo)[axis];
    }

    std::optional<absinfo_t> device_t::get_abs(std::uint16_t axis) const
    {
        const auto event = _event.lock();
        return get_abs_locked(event, axis);
    }

    std::size_t device_t::mt_slots_locked(const locked_event_t &event) const
    {
        return event->mt ? event->mt->size() : 0;
    }

    std::size_t device_t::mt_slots() const
    {
        const auto event = _event.lock();
        return mt_slots_locked(event);
    }

    std::size_t device_t::mt_values(
        std::uint16_t axis, std::span<std::int32_t> into, std::size_t first
    ) const
    {
        if (!is_mt_value(axis))
            return 0;

        const auto event = _event.lock();
        if (!event->mt || first >= event->mt->size())
            return 0;

        const auto count = std::min(event->mt->size() - first, into.size());
        for (std::size_t i = 0; i < count; i++)
            into[i] = event->mt->value(first + i, axis);
        return count;
    }

    std::array<std::int32_t, rep_cnt> device_t::get_repeat() const
    {
        const auto _ = _event.lock();
        return repeat;
    }

    lib::expect<void> device_t::get_keycode(keymap_entry_t &entry) const
    {
        const auto event = _event.lock();

        if (on_getkeycode)
            return on_getkeycode(const_cast<device_t &>(*this), entry);

        if (event->keycodes.empty())
            return std::unexpected { lib::err::invalid_argument };

        const auto index = keymap_index(entry);
        if (!index)
            return std::unexpected { index.error() };

        if (*index >= event->keycodes.size())
            return std::unexpected { lib::err::invalid_argument };

        entry.keycode = event->keycodes[*index];
        entry.index = *index;
        entry.len = sizeof(*index);

        std::memset(entry.scancode, 0, sizeof(entry.scancode));
        std::memcpy(entry.scancode, std::addressof(*index), sizeof(*index));
        return { };
    }

    lib::expect<void> device_t::store_keycode(
        locked_event_t &event, const keymap_entry_t &entry, std::uint32_t &old
    )
    {
        if (on_setkeycode)
            return on_setkeycode(*this, entry, old);

        if (event->keycodes.empty())
            return std::unexpected { lib::err::invalid_argument };

        const auto index = keymap_index(entry);
        if (!index)
            return std::unexpected { index.error() };

        if (*index >= event->keycodes.size())
            return std::unexpected { lib::err::invalid_argument };

        old = std::exchange(event->keycodes[*index], entry.keycode);
        if (old <= key_max)
        {
            supported.set(ev_key, old, false);
            for (const auto code : event->keycodes)
            {
                if (code == old)
                {
                    supported.set(ev_key, old, true);
                    break;
                }
            }
        }

        supported.set(ev_key, entry.keycode, true);
        return { };
    }

    lib::expect<void> device_t::set_keycode(const keymap_entry_t &entry)
    {
        if (entry.keycode > key_max)
            return std::unexpected { lib::err::invalid_argument };

        auto event = _event.lock();

        std::uint32_t old = 0;
        if (const auto ret = store_keycode(event, entry, old); !ret)
            return ret;

        supported.set(ev_key, key_reserved, false);

        const auto code = static_cast<std::uint16_t>(old);
        if (old <= key_max && events.get(ev_key) &&
            !supported.get(ev_key, code) && event->state.get(ev_key, code))
        {
            event->state.set(ev_key, code, false);

            const value_t release[] {
                { ev_key, code, 0 },
                { ev_syn, syn_report, 0 }
            };
            dispatch(event, release);
        }

        return { };
    }

    void device_t::set_mt_slots(std::size_t slots)
    {
        if (slots == 0)
            return;

        auto mt = std::make_unique<mt_t>(slots);
        {
            auto event = _event.lock();
            event->mt.swap(mt);
        }
        set_abs(abs_mt_slot, { 0, 0, static_cast<std::int32_t>(slots) - 1, 0, 0, 0 });
    }

    std::size_t device_t::calc_ev_per_packet() const
    {
        const auto event = _event.lock();
        std::size_t slots = mt_slots_locked(event);

        if (slots == 0)
        {
            if (supported.get(ev_abs, abs_mt_tracking_id))
            {
                if (const auto info = get_abs_locked(event, abs_mt_tracking_id))
                {
                    slots = std::clamp<std::int64_t>(
                        static_cast<std::int64_t>(info->maximum) - info->minimum + 1, 2, 32
                    );
                }
                else slots = 2;
            }
            else if (supported.get(ev_abs, abs_mt_position_x))
                slots = 2;
        }

        std::size_t count = slots + 1;
        if (events.get(ev_abs))
        {
            const auto axes = supported.bits(ev_abs);
            for (std::uint16_t axis = 0; axis < abs_cnt; axis++)
            {
                if (axes.get(axis))
                    count += is_mt_axis(axis) ? slots : 1;
            }
        }

        if (events.get(ev_rel))
            count += supported.bits(ev_rel).count();
        return count + 7;
    }

    bool device_t::admit(locked_event_t &event, value_t &val)
    {
        if (val.type >= ev_cnt || !events.get(val.type))
            return false;

        if (val.type == ev_syn)
        {
            switch (val.code)
            {
                case syn_report:
                case syn_mt_report:
                    return true;
                case syn_config:
                    if (on_event)
                        on_event(*this, val);
                    return true;
                default:
                    return false;
            }
        }

        if (val.type != ev_rep && code_count(val.type) != 0 && !supported.get(val.type, val.code))
            return false;

        switch (val.type)
        {
            case ev_key:
            case ev_sw:
            case ev_led:
            {
                if (val.type == ev_key && val.value == 2) // autorepeat
                    break;

                const bool down = val.value != 0;
                if (event->state.get(val.type, val.code) == down)
                    return false;

                event->state.set(val.type, val.code, down);

                if (val.type == ev_led && on_event)
                    on_event(*this, val);
                break;
            }
            case ev_snd:
            {
                const bool down = val.value != 0;
                if (event->state.get(ev_snd, val.code) != down)
                    event->state.set(ev_snd, val.code, down);

                if (on_event)
                    on_event(*this, val);
                break;
            }
            case ev_msc:
            {
                if (on_event)
                    on_event(*this, val);
                break;
            }
            case ev_rep:
            {
                if (val.code >= repeat.size() || val.value < 0 ||
                    repeat[val.code] == val.value)
                    return false;

                repeat[val.code] = val.value;
                if (on_event)
                    on_event(*this, val);
                break;
            }
            case ev_rel:
            {
                if (val.value == 0)
                    return false;
                break;
            }
            case ev_abs:
            {
                if (val.code == abs_mt_slot)
                {
                    if (event->mt && val.value >= 0 &&
                        static_cast<std::size_t>(val.value) < event->mt->size())
                        event->mt->select(val.value);
                    return false;
                }

                const bool mt_value = is_mt_value(val.code);

                std::int32_t *old = nullptr;
                if (!mt_value)
                {
                    if (event->absinfo)
                        old = std::addressof((*event->absinfo)[val.code].value);
                }
                else if (event->mt)
                    old = std::addressof(event->mt->value(val.code));

                if (old)
                {
                    const auto fuzz = event->absinfo ? (*event->absinfo)[val.code].fuzz : 0;
                    val.value = defuzz(val.value, *old, fuzz);
                    if (*old == val.value)
                        return false;

                    *old = val.value;
                }

                if (mt_value && event->mt && event->absinfo)
                {
                    auto &staged = (*event->absinfo)[abs_mt_slot].value;
                    if (staged != static_cast<std::int32_t>(event->mt->slot()))
                    {
                        staged = event->mt->slot();
                        event->emit_slot = true;
                    }
                }
                break;
            }
            default:
                break;
        }
        return true;
    }

    const stamp_t &device_t::stamp_now(locked_event_t &event)
    {
        if (!event->stamped)
        {
            event->stamp = stamp_t::capture();
            event->stamped = true;
        }
        return event->stamp;
    }

    void device_t::set_stamp(const timespec &mono)
    {
        auto event = _event.lock();
        event->stamp = stamp_t::from(mono);
        event->stamped = true;
    }

    void device_t::dispatch(locked_event_t &event, std::span<const value_t> packet)
    {
        const auto &stamp = stamp_now(event);

        const rcu::read_guard _ { };
        if (const auto ptr = _grab.dereference())
            ptr->receive(*this, stamp, packet);
        else if (const auto ptr = _handles.dereference())
        {
            for (const auto handle : *ptr)
            {
                if (handle->open.load(std::memory_order_relaxed) == 0)
                    continue;

                handle->receive(*this, stamp, packet);
            }
        }
    }

    void device_t::handle_event(
        locked_event_t &event,
        std::uint16_t type, std::uint16_t code, std::int32_t value
    )
    {
        const bool flush = (type == ev_syn && code == syn_report);

        value_t val { type, code, value };
        event->emit_slot = false;

        if (admit(event, val))
        {
            if (event->emit_slot)
            {
                event->vals[event->nvals++] = {
                    ev_abs, abs_mt_slot, static_cast<std::int32_t>(event->mt->slot())
                };
            }
            event->vals[event->nvals++] = val;
        }

        if (flush)
        {
            // just syn_report means that nothing was admitted
            if (event->nvals >= 2)
                dispatch(event, { event->vals.data(), event->nvals });

            event->nvals = 0;
            event->stamped = false;
        }
        // space for abs_mt_slot, its value and syn_report
        else if (event->nvals >= event->vals.size() - 2)
        {
            event->vals[event->nvals++] = { ev_syn, syn_report, 0 };
            dispatch(event, { event->vals.data(), event->nvals });
            event->nvals = 0;
        }
    }

    void device_t::release_keys(locked_event_t &event)
    {
        if (!events.get(ev_key) || event->vals.empty())
            return;

        bool sync = false;
        const auto keys = event->state.bits(ev_key);

        for (std::size_t code = 0; code < keys.size(); code++)
        {
            if (!keys.get(code))
                continue;

            handle_event(event, ev_key, static_cast<std::uint16_t>(code), 0);
            sync = true;
        }

        if (sync)
            handle_event(event, ev_syn, syn_report, 0);
    }

    void device_t::report(std::span<const value_t> packet)
    {
        if (packet.empty() || !registered() || inhibited())
            return;

        auto event = _event.lock();
        for (const auto &val : packet)
            handle_event(event, val.type, val.code, val.value);
    }

    void device_t::report(std::uint16_t type, std::uint16_t code, std::int32_t value)
    {
        if (!registered() || inhibited())
            return;

        auto event = _event.lock();
        handle_event(event, type, code, value);
    }

    void device_t::snapshot(std::uint16_t type, lib::bitmap_view into) const
    {
        if (into.size() == 0)
            return;

        const auto event = _event.lock();
        const auto from = event->state.bits(type);
        if (from.size() == 0)
            return;

        std::memcpy(
            into.data(), from.data(),
            std::min(into.size_bytes(), from.size_bytes())
        );
    }

    lib::expect<void> device_t::add_handle(handle_t *handle)
    {
        if (!handle)
            return std::unexpected { lib::err::invalid_argument };

        const std::unique_lock _ { _lock };
        if (!registered())
            return std::unexpected { lib::err::no_such_device };

        const auto current = _handles.unsafe_load();
        if (current && std::ranges::contains(*current, handle))
            return std::unexpected { lib::err::already_exists };

        rcu::updater next { _handles };
        next->push_back(handle);
        next.commit();
        return { };
    }

    void device_t::detach_handle(handle_t *handle)
    {
        while (handle->open.load(std::memory_order_relaxed) != 0)
            close_locked(handle);
    }

    void device_t::unlink_handle(handle_t *handle)
    {
        if (_grab.unsafe_load() == handle)
            _grab.assign(nullptr);

        {
            rcu::updater next { _handles };
            if (lib::erase_if(*next, [&](handle_t *ptr) { return ptr == handle; }) == 0)
                return;

            next.commit();
        }

        rcu::synchronise();
        detach_handle(handle);
    }

    void device_t::unlink_all()
    {
        _grab.assign(nullptr);

        std::vector<handle_t *> removed;
        {
            const auto current = _handles.unsafe_load();
            if (!current || current->empty())
                return;

            removed = *current;

            rcu::updater next { _handles };
            next->clear();
            next.commit();
        }

        rcu::synchronise();
        for (const auto handle : removed)
            detach_handle(handle);
    }

    void device_t::remove_handle(handle_t *handle)
    {
        if (!handle)
            return;

        const std::unique_lock _ { _lock };
        unlink_handle(handle);
    }

    auto device_t::find_owned(const consumer_t &consumer)
    {
        return std::ranges::find_if(_owned, [&](const auto &pair) {
            return pair.first == std::addressof(consumer);
        });
    }

    lib::expect<void> device_t::open_locked(handle_t *handle)
    {
        handle->open++;

        if (_open_files++ || inhibited() || !on_open)
            return { };

        const auto ret = on_open(*this);
        if (!ret)
        {
            _open_files--;
            handle->open--;
        }
        return ret;
    }

    void device_t::close_locked(handle_t *handle)
    {
        if (handle->open == 0)
            return;

        lib::bug_on(_open_files == 0);

        if (--handle->open == 0 && _grab.unsafe_load() == handle)
            _grab.assign(nullptr);

        if (--_open_files == 0 && !inhibited() && on_close)
            on_close(*this);
    }

    lib::expect<void> device_t::open_device(handle_t *handle)
    {
        const std::unique_lock _ { _lock };
        return open_locked(handle);
    }

    void device_t::close_device(handle_t *handle)
    {
        const std::unique_lock _ { _lock };
        close_locked(handle);
    }

    void device_t::attach_consumer(consumer_t &consumer)
    {
        {
            const std::unique_lock _ { _lock };
            if (find_owned(consumer) != _owned.end())
                return;
        }

        if (!consumer.match(*this))
            return;

        auto handle = consumer.connect(*this);
        if (!handle)
            return;

        if (const auto ret = add_handle(handle.get()); !ret)
        {
            lib::error("input: could not attach consumer '{}' to '{}'", consumer.name, name);
            return;
        }

        if (consumer.always_open())
        {
            if (const auto ret = open_device(handle.get()); !ret)
            {
                lib::error("input: could not open '{}' for consumer '{}'", name, consumer.name);
                remove_handle(handle.get());
                return;
            }
        }

        const std::unique_lock _ { _lock };
        _owned.emplace_back(std::addressof(consumer), std::move(handle));
    }

    void device_t::detach_consumer(consumer_t &consumer)
    {
        std::shared_ptr<handle_t> owned;
        {
            const std::unique_lock _ { _lock };

            const auto it = find_owned(consumer);
            if (it == _owned.end())
                return;

            owned = std::move(it->second);
            _owned.erase(it);

            owned->revoke();
            unlink_handle(owned.get());
        }
    }

    lib::expect<void> register_consumer(consumer_t &consumer)
    {
        const std::unique_lock _ { registry_lock };

        if (std::ranges::contains(consumers, std::addressof(consumer)))
            return std::unexpected { lib::err::already_exists };

        consumers.push_back(std::addressof(consumer));

        for (const auto &dev : locked_devices())
            dev->attach_consumer(consumer);

        lib::debug("input: registered consumer '{}'", consumer.name);
        return { };
    }

    void unregister_consumer(consumer_t &consumer)
    {
        const std::unique_lock _ { registry_lock };

        lib::erase(consumers, std::addressof(consumer));

        for (const auto &dev : locked_devices())
            dev->detach_consumer(consumer);

        lib::debug("input: unregistered consumer '{}'", consumer.name);
    }

    lib::expect<void> device_t::grab(handle_t *handle)
    {
        if (!handle)
            return std::unexpected { lib::err::invalid_argument };

        const std::unique_lock _ { _lock };

        if (_grab.unsafe_load())
            return std::unexpected { lib::err::target_is_busy };

        const auto current = _handles.unsafe_load();
        if (!current || !std::ranges::contains(*current, handle))
            return std::unexpected { lib::err::invalid_argument };

        _grab.assign(handle);
        return { };
    }

    lib::expect<void> device_t::ungrab(handle_t *handle)
    {
        const std::unique_lock _ { _lock };

        if (_grab.unsafe_load() != handle)
            return std::unexpected { lib::err::invalid_argument };

        _grab.assign(nullptr);
        return { };
    }

    lib::expect<void> device_t::inject(
        handle_t *handle,
        std::uint16_t type, std::uint16_t code, std::int32_t value
    )
    {
        if (type >= ev_cnt || !events.get(type) || !registered())
            return { };

        auto event = _event.lock();
        {
            const rcu::read_guard _ { };
            if (const auto grab = _grab.dereference(); grab && grab != handle)
                return { };
        }

        handle_event(event, type, code, value);
        return { };
    }

    std::string device_t::get_modalias() const
    {
        auto ret = fmt::format(
            "input:b{:04X}v{:04X}p{:04X}e{:04X}-",
            ident.bustype, ident.vendor, ident.product, ident.version
        );

        const auto bits = [&ret](char name, const lib::const_bitmap_view view, std::size_t min, std::size_t max)
        {
            ret += name;
            for (std::size_t i = min; i < max; i++)
            {
                if (i % 8 == 0 && view.data()[i / 8] == 0)
                {
                    i += 7;
                    continue;
                }

                if (view.get(i))
                    fmt::format_to(std::back_inserter(ret), "{:X},", i);
            }
        };

        bits('e', events, 0, ev_max);
        bits('k', supported.bits(ev_key), key_min_interesting, key_max);
        bits('r', supported.bits(ev_rel), 0, rel_max);
        bits('a', supported.bits(ev_abs), 0, abs_max);
        bits('m', supported.bits(ev_msc), 0, msc_max);
        bits('l', supported.bits(ev_led), 0, led_max);
        bits('s', supported.bits(ev_snd), 0, snd_max);
        bits('f', supported.bits(ev_ff), 0, ff_max);
        bits('w', supported.bits(ev_sw), 0, sw_max);
        return ret;
    }

    lib::expect<void> device_t::register_device()
    {
        if (registered())
            return std::unexpected { lib::err::already_exists };

        modalias = get_modalias();

        // for abs_mt_slot and syn_report
        _ev_per_packet = calc_ev_per_packet();
        {
            std::vector<value_t> vals(_ev_per_packet + 2);
            auto event = _event.lock();
            event->vals = std::move(vals);
            event->nvals = 0;
        }

        auto self = std::static_pointer_cast<device_t>(as_shared());

        if (const auto ret = dev::register_device(self); !ret)
            return ret;

        _registered.store(true, std::memory_order_release);

        const std::unique_lock _ { registry_lock };
        lib::erase_if(devices, [](const std::weak_ptr<device_t> &weak) {
            return weak.expired();
        });
        devices.push_back(self);

        for (const auto consumer : consumers)
            attach_consumer(*consumer);

        lib::info("input: registered device: '{}'", name);
        return { };
    }

    void device_t::unregister_device()
    {
        if (!_registered.exchange(false, std::memory_order_acq_rel))
            return;

        {
            auto event = _event.lock();
            release_keys(event);
        }

        std::vector<std::shared_ptr<handle_t>> owned;
        {
            const std::unique_lock _ { registry_lock };

            lib::erase_if(devices, [this](const std::weak_ptr<device_t> &weak) {
                const auto dev = weak.lock();
                return !dev || dev.get() == this;
            });

            const std::unique_lock _ { _lock };

            if (const auto ptr = _handles.unsafe_load())
            {
                for (const auto handle : *ptr)
                    handle->revoke();
            }

            unlink_all();

            owned.reserve(_owned.size());
            for (auto &[_, handle] : _owned)
                owned.push_back(std::move(handle));
            _owned.clear();
        }
        owned.clear();

        {
            auto event = _event.lock();
            event->nvals = 0;
            event->state.clear();
            event->stamped = false;
        }

        dev::unregister_device(std::static_pointer_cast<dev::device_t>(as_shared()));
        lib::info("input: unregistered device: '{}'", name);
    }

    lib::initgraph::stage *class_registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "input.class-registered",
            lib::initgraph::postsched_init_engine
        };
        return std::addressof(stage);
    }

    namespace
    {
        lib::initgraph::task register_task
        {
            "input.register-class",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require { dev::core_registered_stage() },
            lib::initgraph::entail { class_registered_stage() },
            [] {
                lib::bug_on(!dev::register_class(get_class()));
            }
        };
    } // namespace
} // namespace input
