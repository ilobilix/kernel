// Copyright (C) 2024-2026  ilobilo

// the entire input subsystem is based on linux

// TODO: force feedback

export module system.input;

import system.chrono;
import system.sched;
import system.dev;
import system.rcu;
import lib;
import std;

export import :codes;
export import :spec;

namespace input
{
    dev::class_t &get_class();
    dev::ktype_t &get_ktype();
} // namespace input

export namespace input
{
    constexpr std::size_t code_count(std::uint16_t type)
    {
        switch (type)
        {
            case ev_key:
                return key_cnt;
            case ev_rel:
                return rel_cnt;
            case ev_abs:
                return abs_cnt;
            case ev_msc:
                return msc_cnt;
            case ev_sw:
                return sw_cnt;
            case ev_led:
                return led_cnt;
            case ev_snd:
                return snd_cnt;
            case ev_rep:
                return rep_cnt;
            case ev_ff:
                return ff_cnt;
            default:
                return 0;
        }
    }

    constexpr bool is_mt_value(std::uint32_t axis)
    {
        return axis >= abs_mt_touch_major && axis <= abs_mt_tool_y;
    }

    constexpr bool is_mt_axis(std::uint32_t axis)
    {
        return axis == abs_mt_slot || is_mt_value(axis);
    }

    class mt_t
    {
        static constexpr std::size_t mt_axis_count = abs_mt_tool_y - abs_mt_touch_major + 1;

        private:
        std::vector<std::array<std::int32_t, mt_axis_count>> _slots;
        std::size_t _slot;

        public:
        mt_t(std::size_t slots) : _slots { slots }, _slot { 0 }
        {
            _slots.resize(slots);
            for (auto &slot : _slots)
                slot[abs_mt_tracking_id - abs_mt_touch_major] = -1;
        }

        std::size_t size() const { return _slots.size(); }
        std::size_t slot() const { return _slot; }

        void select(std::size_t slot) { _slot = slot; }

        std::int32_t &value(std::uint16_t axis)
        {
            return _slots[_slot][axis - abs_mt_touch_major];
        }

        std::int32_t value(std::size_t slot, std::uint16_t axis) const
        {
            return _slots[slot][axis - abs_mt_touch_major];
        }
    };

    class codeset_t
    {
        private:
        lib::static_bitmap<key_cnt> _key;
        lib::static_bitmap<rel_cnt> _rel;
        lib::static_bitmap<abs_cnt> _abs;
        lib::static_bitmap<msc_cnt> _msc;
        lib::static_bitmap<led_cnt> _led;
        lib::static_bitmap<snd_cnt> _snd;
        lib::static_bitmap<ff_cnt> _ff;
        lib::static_bitmap<sw_cnt> _sw;
        lib::static_bitmap<rep_cnt> _rep;

        public:
        constexpr auto bits(this auto &self, std::uint16_t type)
        {
            switch (type)
            {
                case ev_key:
                    return self._key.view();
                case ev_rel:
                    return self._rel.view();
                case ev_abs:
                    return self._abs.view();
                case ev_msc:
                    return self._msc.view();
                case ev_led:
                    return self._led.view();
                case ev_snd:
                    return self._snd.view();
                case ev_ff:
                    return self._ff.view();
                case ev_sw:
                    return self._sw.view();
                case ev_rep:
                    return self._rep.view();
                default:
                    return decltype(self._key.view()) { };
            }
        }

        constexpr bool get(std::uint16_t type, std::uint16_t code) const
        {
            const auto view = bits(type);
            return code < view.size() && view.get(code);
        }

        constexpr bool set(std::uint16_t type, std::uint16_t code, bool value = true)
        {
            auto view = bits(type);
            if (code >= view.size())
                return false;

            view.set(code, value);
            return true;
        }

        constexpr void clear()
        {
            _key.clear();
            _rel.clear();
            _abs.clear();
            _msc.clear();
            _led.clear();
            _snd.clear();
            _ff.clear();
            _sw.clear();
            _rep.clear();
        }
    };

    struct stamp_t
    {
        timespec mono;
        std::uint64_t real_off;
        std::uint64_t boot_off;

        static stamp_t from(const timespec &mono)
        {
            return {
                mono,
                chrono::offset_ns(chrono::realtime),
                chrono::offset_ns(chrono::boottime)
            };
        }

        static stamp_t capture()
        {
            return from(chrono::now(chrono::monotonic));
        }

        timeval time(chrono::type clockid) const
        {
            const auto ts = [this, clockid] -> timespec {
                switch (clockid)
                {
                    case chrono::realtime:
                        return mono.to_ns() + real_off;
                    case chrono::boottime:
                        return mono.to_ns() + boot_off;
                    default:
                        return mono;
                }
            } ();
            return { ts.tv_sec, static_cast<suseconds_t>(ts.tv_nsec / 1'000) };
        }
    };

    class device_t;
    struct handle_t
    {
        std::atomic_size_t open = 0;

        virtual ~handle_t() = default;

        virtual void revoke() { }
        virtual void receive(
            device_t &dev, const stamp_t &stamp, std::span<const value_t> packet
        ) = 0;
    };

    struct consumer_t
    {
        const std::string name;
        consumer_t(std::string name) : name { std::move(name) } { }

        virtual ~consumer_t() = default;

        virtual bool match(const device_t &dev) = 0;
        virtual std::shared_ptr<handle_t> connect(device_t &dev) = 0;

        virtual bool always_open() const { return true; }
    };

    class device_t : public dev::device_t
    {
        private:
        mutable sched::mutex_t _lock;

        struct repeat_timer_t : sched::timer_t
        {
            std::weak_ptr<device_t> dev;

            void expired(std::uint64_t missed) override;
            void notify() override;
        };

        enum class repeat_action : std::uint8_t
        {
            none,
            start,
            next,
            stop
        };

        struct repeat_req_t
        {
            repeat_action action = repeat_action::none;
            std::uint64_t seq = 0;
            std::uint32_t delay_ms = 0;
        };

        // TODO: move supported and repeat in here
        struct event_t
        {
            std::vector<value_t> vals;
            std::size_t nvals = 0;

            std::uint16_t repeat_key = key_reserved;
            bool repeat_active = false;
            repeat_action repeat_pending = repeat_action::none;
            std::uint64_t repeat_seq = 0;

            codeset_t state;
            bool emit_slot = false;

            stamp_t stamp { };
            bool stamped = false;

            std::unique_ptr<std::array<absinfo_t, abs_cnt>> absinfo;
            std::unique_ptr<mt_t> mt;
            std::vector<std::uint32_t> keycodes;
        };
        mutable lib::locker<event_t, lib::spinlock_irq> _event;
        using locked_event_t = decltype(_event.lock());

        std::shared_ptr<repeat_timer_t> _repeat;
        bool _softrepeat;

        lib::spinlock _repeat_lock;
        std::uint64_t _repeat_applied;

        rcu::owner<rcu::box<std::vector<handle_t *>>> _handles;
        rcu::pointer<handle_t> _grab;

        std::vector<std::pair<consumer_t *, std::shared_ptr<handle_t>>> _owned;

        std::atomic_bool _registered;
        std::atomic_bool _inhibited;

        std::size_t _open_files;
        std::size_t _index;

        // number of events between syn_reports
        std::size_t _ev_per_packet;
        std::size_t calc_ev_per_packet() const;

        lib::expect<void> store_keycode(
            locked_event_t &event, const keymap_entry_t &entry, std::uint32_t &old
        );

        const stamp_t &stamp_now(locked_event_t &event);

        bool admit(locked_event_t &event, value_t &value);
        void dispatch(locked_event_t &event, std::span<const value_t> packet);

        void handle_event(
            locked_event_t &event,
            std::uint16_t type, std::uint16_t code, std::int32_t value
        );

        void release_keys(locked_event_t &event);
        void release_mt_slots(locked_event_t &event);

        void alloc_absinfo();

        std::size_t mt_slots_locked(const locked_event_t &event) const;
        std::optional<absinfo_t> get_abs_locked(
            const locked_event_t &event, std::uint16_t axis
        ) const;

        void start_repeat(locked_event_t &event, std::uint16_t code);
        void stop_repeat(locked_event_t &event);

        repeat_req_t take_repeat(locked_event_t &event);
        void apply_repeat(const repeat_req_t &req);
        void repeat_tick();

        // called with _lock acquired
        void detach_handle(handle_t *handle);
        void unlink_handle(handle_t *handle);
        void unlink_all();
        auto find_owned(const consumer_t &consumer);

        lib::expect<void> open_locked(handle_t *handle);
        void close_locked(handle_t *handle);

        protected:
        device_t(std::string_view name, std::weak_ptr<dev::kobject_t> parent);

        public:
        std::string desc;
        std::string phys;
        std::string uniq;
        id_t ident;

        lib::static_bitmap<prop_cnt> props;
        lib::static_bitmap<ev_cnt> events;
        codeset_t supported;

        // autorepeat delay and period in ms
        std::array<std::int32_t, rep_cnt> repeat;

        std::function<void (device_t &, const value_t &)> on_event;
        std::function<lib::expect<void> (device_t &)> on_open;
        std::function<void (device_t &)> on_close;

        std::function<lib::expect<void> (device_t &, keymap_entry_t &)> on_getkeycode;
        std::function<
            lib::expect<void> (device_t &, const keymap_entry_t &, std::uint32_t &)
        > on_setkeycode;

        device_t(const device_t &) = delete;
        device_t &operator=(const device_t &) = delete;

        template<typename ...Args>
        device_t(lib::private_t<device_t>, Args &&...args)
            : device_t { std::forward<Args>(args)... } { };

        static lib::expect<std::shared_ptr<device_t>> create(std::weak_ptr<dev::kobject_t> parent);

        std::size_t get_ev_per_packet() const { return _ev_per_packet; }

        // called before register_device
        void set_cap(std::uint16_t type, std::uint16_t code);
        void set_prop(std::uint16_t prop) { props.set(prop, true); }
        void set_abs(std::uint16_t axis, const absinfo_t &info);
        void set_mt_slots(std::size_t slots);
        void set_keymap(std::span<const std::uint32_t> keycodes);

        lib::expect<void> set_absinfo(std::uint16_t axis, const absinfo_t &info);
        std::optional<absinfo_t> get_abs(std::uint16_t axis) const;

        std::size_t mt_slots() const;
        std::size_t mt_values(
            std::uint16_t axis, std::span<std::int32_t> into, std::size_t first = 0
        ) const;

        std::array<std::int32_t, rep_cnt> get_repeat() const;

        std::size_t keycodemax() const
        {
            const auto event = _event.lock();
            return event->keycodes.size();
        }

        std::uint32_t keycode(std::size_t index) const
        {
            const auto event = _event.lock();
            return index < event->keycodes.size() ? event->keycodes[index] : key_reserved;
        }

        lib::expect<void> get_keycode(keymap_entry_t &entry) const;
        lib::expect<void> set_keycode(const keymap_entry_t &entry);

        void set_stamp(const timespec &mono);

        // only thign the drivers use
        void report(std::span<const value_t> packet);
        void report(std::uint16_t type, std::uint16_t code, std::int32_t value);
        void sync() { report(ev_syn, syn_report, 0); }

        void snapshot(std::uint16_t type, lib::bitmap_view into) const;

        bool inhibited() const { return _inhibited.load(std::memory_order_relaxed); }
        lib::expect<void> inhibit(bool value);

        lib::expect<void> add_handle(handle_t *handle);
        void remove_handle(handle_t *handle);

        lib::expect<void> open_device(handle_t *handle);
        void close_device(handle_t *handle);

        // called with registry lock acquired
        void attach_consumer(consumer_t &consumer);
        void detach_consumer(consumer_t &consumer);

        lib::expect<void> grab(handle_t *handle);
        lib::expect<void> ungrab(handle_t *handle);

        // from userspace
        lib::expect<void> inject(
            handle_t *handle,
            std::uint16_t type, std::uint16_t code, std::int32_t value
        );

        std::string get_modalias() const;

        lib::expect<void> register_device();
        void unregister_device();

        bool registered() const { return _registered.load(std::memory_order_acquire); }
    };

    lib::expect<void> register_consumer(consumer_t &consumer);
    void unregister_consumer(consumer_t &consumer);

    lib::expect<kbmode> get_kbmode(std::size_t console);
    lib::expect<void> set_kbmode(std::size_t console, kbmode mode);
} // export namespace input
