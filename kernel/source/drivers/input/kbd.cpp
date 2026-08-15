// Copyright (C) 2024-2026  ilobilo

module drivers.input;

import drivers.output.vt;
import magic_enum;

import :keymap;

namespace input::kbd
{
    namespace vt = output::vt;
    namespace
    {
        using namespace magic_enum::bitwise_operators;
        constexpr auto defmflags = mode_flag::autorepeat | mode_flag::meta_escape;

        struct handler_ctx_t
        {
            std::size_t console;
            std::size_t target = 0;
            bool previous = false;

            void activate(std::size_t index)
            {
                if (index == 0 || index > vt::num_consoles)
                    return;
                target = index;
                previous = false;
            }

            void activate_previous()
            {
                target = 0;
                previous = true;
            }
        };

        std::uint8_t ledioctl = 0;
        struct keyboard_t
        {
            std::uint8_t lock = 0;
            std::uint8_t slock = 0;

            // 1 means change leds on ioctl
            std::uint8_t ledmode : 1 = 0;
            std::uint8_t ledstate : 4 = 0;
            std::uint8_t defledstate : 4 = 0;
            std::uint8_t kbdmode : 3 = k_unicode;
            mode_flag mode_flags : 5 = defmflags;

            bool get_mode_flag(mode_flag flag) const
            {
                return (mode_flags & flag) != mode_flag::none;
            }

            bool get_led_state(kbled led) const
            {
                return (ledstate & std::to_underlying(led)) != 0;
            }

            std::uint8_t get_led_lights() const
            {
                return ledmode ? ledioctl : ledstate;
            }
        };
        std::array<keyboard_t, vt::num_consoles> kbds { };

        lib::static_bitmap<key_cnt> keys_down { };
        std::array<std::uint8_t, nr_shift> shift_down { };
        std::vector<std::weak_ptr<device_t>> keyboards;

        int shift_state = 0;
        bool rep = false;
        bool dead_key_next = false;
        bool npadch_active = false;

        std::uint32_t npadch_value = 0;
        std::uint32_t diacr = 0;

        std::array<std::byte, 16> buffer;
        std::size_t buf_idx = 0;

        lib::spinlock_irq lock;

        keyboard_t *get_keyboard(std::size_t console)
        {
            if (console == 0 || console > kbds.size())
                return nullptr;
            return &kbds[console - 1];
        }

        bool is_raw(device_t &dev)
        {
            if (!dev.events.get(ev_msc) || !dev.supported.get(ev_msc, msc_raw))
                return false;
            return dev.ident.bustype == bus_i8042 &&
                dev.ident.vendor == 0x0001 && dev.ident.product == 0x0001;
        }

        int csi_mod()
        {
            int modifier = 1;
            if (shift_state & (1 << kg_shift))
                modifier += 1;
            if (shift_state & (1 << kg_alt))
                modifier += 2;
            if (shift_state & (1 << kg_ctrl))
                modifier += 4;
            return modifier;
        }

        void enqueue(int val)
        {
            lib::bug_on(buf_idx == buffer.size());
            buffer[buf_idx++] = static_cast<std::byte>(val);
        }

        void enqueue(std::string_view str)
        {
            for (const auto ch : str)
                enqueue(ch);
        }

        void enqueue(char key, bool app)
        {
            enqueue('\033');
            enqueue(app ? 'O' : '[');
            enqueue(key);
        }

        void submit(std::size_t console)
        {
            if (buf_idx == 0)
                return;
            vt::receive_input(console, std::span { buffer } .first(buf_idx));
            buf_idx = 0;
        }

        std::uint32_t to_uni(std::uint8_t val)
        {
            const auto uni = translations[user_map][val];
            return uni == (0xF000 | val) ? val : uni;
        }

        void enqueue_uni(const keyboard_t &kbd, std::uint32_t val)
        {
            if (kbd.kbdmode != k_unicode)
            {
                if (val <= std::numeric_limits<std::uint8_t>::max() && to_uni(val) == val)
                {
                    enqueue(val);
                    return;
                }

                for (std::size_t i = 0; i < std::size(translations[user_map]); i++)
                {
                    if (to_uni(i) == val)
                    {
                        enqueue(i);
                        return;
                    }
                }
                return;
            }

            if (val < 0x80)
                enqueue(val);
            else if (val < 0x800)
            {
                enqueue(0xC0 | (val >> 6));
                enqueue(0x80 | (val & 0x3F));
            }
            else if (val < 0x10000)
            {
                if ((val >= 0xD800 && val < 0xE000) || val == 0xFFFF)
                    return;

                enqueue(0xE0 | (val >> 12));
                enqueue(0x80 | ((val >> 6) & 0x3F));
                enqueue(0x80 | (val & 0x3F));
            }
            else if (val < 0x110000)
            {
                enqueue(0xF0 | (val >> 18));
                enqueue(0x80 | ((val >> 12) & 0x3F));
                enqueue(0x80 | ((val >> 6) & 0x3F));
                enqueue(0x80 | (val & 0x3F));
            }
        }

        void compute_shiftstate()
        {
            shift_state = 0;
            shift_down.fill(0);

            for (std::size_t i = 0; i < std::min(nr_keys, keys_down.size()); i++)
            {
                if (!keys_down.get(i))
                    continue;

                const auto sym = key_uni(key_maps[0][i]);
                if (key_type(sym) != kt_shift && key_type(sym) != kt_slock)
                    continue;

                auto val = key_val(sym);
                if (val == key_val(k_capsshift))
                    val = key_val(k_shift);

                shift_down[val]++;
                shift_state |= (1ul << val);
            }
        }

        std::uint32_t combine_diacr(keyboard_t &kbd, std::uint32_t val)
        {
            const auto current = std::exchange(diacr, 0);
            for (const auto &entry : accent_table)
            {
                if (entry.diacr == current && entry.base == val)
                    return entry.result;
            }

            if (val == ' ' || val == current)
                return current;

            enqueue_uni(kbd, current);
            return val;
        }

        void handle_cur(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up);
        void handle_dead2(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up);

        void handle_unicode(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx);
            if (up)
                return;

            if (diacr)
                val = combine_diacr(kbd, val);

            if (dead_key_next)
            {
                dead_key_next = false;
                diacr = val;
                return;
            }

            enqueue_uni(kbd, val);
        }

        void handle_self(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            handle_unicode(ctx, kbd, to_uni(val), up);
        }

        void handle_fn(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx, kbd);
            if (!up && val < std::size(func_table))
                enqueue(func_table[val]);
        }

        void handle_spec(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            if (up || val > spec_bare_num)
                return;

            if ((kbd.kbdmode == k_raw || kbd.kbdmode == k_mediumraw || kbd.kbdmode == k_off) &&
                val != spec_sak)
                return;

            switch (val)
            {
                case spec_null:
                    compute_shiftstate();
                    break;
                case spec_enter:
                    if (diacr)
                        enqueue_uni(kbd, std::exchange(diacr, 0));
                    enqueue('\r');
                    if (kbd.get_mode_flag(mode_flag::newline))
                        enqueue('\n');
                    break;
                case spec_last_console:
                    ctx.activate_previous();
                    break;
                case spec_caps:
                    if (!rep)
                        kbd.ledstate ^= std::to_underlying(k_capslock);
                    break;
                case spec_num:
                    if (kbd.get_mode_flag(mode_flag::app_keypad))
                        enqueue('P', true);
                    else if (!rep)
                        kbd.ledstate ^= std::to_underlying(k_numlock);
                    break;
                case spec_caps_on:
                    if (!rep)
                        kbd.ledstate |= std::to_underlying(k_capslock);
                    break;
                case spec_compose:
                    dead_key_next = true;
                    break;
                case spec_prev_console:
                    ctx.activate(ctx.console == 1 ? vt::num_consoles : ctx.console - 1);
                    break;
                case spec_next_console:
                    ctx.activate(ctx.console == vt::num_consoles ? 1 : ctx.console + 1);
                    break;
                case spec_bare_num:
                    if (!rep)
                        kbd.ledstate ^= std::to_underlying(k_numlock);
                    break;
                default:
                    // TODO: the rest
                    break;
            }
        }

        void handle_pad(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            static constexpr std::string_view pad_chars { "0123456789+-*/\r,.?()" };
            static constexpr std::string_view app_map { "pqrstuvwxylSRQMnnmPQS" };

            if (up || val >= pad_count)
                return;

            if (kbd.get_mode_flag(mode_flag::app_keypad) && !shift_down[kg_shift])
            {
                enqueue(app_map[val], true);
                return;
            }

            if (!kbd.get_led_state(k_numlock))
            {
                switch (val)
                {
                    case pad_comma:
                    case pad_dot:
                        handle_fn(ctx, kbd, fn_remove, 0);
                        return;
                    case pad_0:
                        handle_fn(ctx, kbd, fn_insert, 0);
                        return;
                    case pad_1:
                        handle_fn(ctx, kbd, fn_select, 0);
                        return;
                    case pad_2:
                        handle_cur(ctx, kbd, cur_down, 0);
                        return;
                    case pad_3:
                        handle_fn(ctx, kbd, fn_page_down, 0);
                        return;
                    case pad_4:
                        handle_cur(ctx, kbd, cur_left, 0);
                        return;
                    case pad_5:
                        enqueue('G', kbd.get_mode_flag(mode_flag::app_keypad));
                        return;
                    case pad_6:
                        handle_cur(ctx, kbd, cur_right, 0);
                        return;
                    case pad_7:
                        handle_fn(ctx, kbd, fn_find, 0);
                        return;
                    case pad_8:
                        handle_cur(ctx, kbd, cur_up, 0);
                        return;
                    case pad_9:
                        handle_fn(ctx, kbd, fn_page_up, 0);
                        return;
                    default:
                        break;
                }
            }

            enqueue(pad_chars[val]);
            if (val == pad_enter && kbd.get_mode_flag(mode_flag::newline))
                enqueue('\n');
        }

        void handle_dead(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            static constexpr std::string_view dead_chars = "`'^~\",_U.*=cki#o!?+-)(:n;$@";
            if (val < dead_chars.size())
                handle_dead2(ctx, kbd, dead_chars[val], up);
        }

        void handle_cons(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(kbd);
            if (!up)
                ctx.activate(val + 1);
        }

        void handle_cur(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx);
            if (up || val >= cur_count)
                return;

            static constexpr std::string_view cursor_chars = "BDCA";
            if (const auto modifier = csi_mod(); modifier > 1)
            {
                enqueue("\033[1;");
                enqueue('0' + modifier);
                enqueue(cursor_chars[val]);
            }
            else enqueue(cursor_chars[val], kbd.get_mode_flag(mode_flag::app_cursor));
        }

        void handle_shift(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx);
            if (rep || val >= shift_down.size())
                return;

            const auto old_state = shift_state;
            if (val == kg_capsshift)
            {
                val = kg_shift;
                if (!up)
                    kbd.ledstate &= ~k_capslock;
            }

            if (up)
            {
                if (shift_down[val])
                    shift_down[val]--;
            }
            else shift_down[val]++;

            if (shift_down[val])
                shift_state |= (1 << val);
            else
                shift_state &= ~(1 << val);

            if (up && shift_state != old_state && npadch_active)
            {
                if (kbd.kbdmode == k_unicode)
                    enqueue_uni(kbd, npadch_value);
                else
                    enqueue(npadch_value & 0xFF);
                npadch_active = false;
            }
        }

        void handle_meta(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx);
            if (up)
                return;

            if (kbd.get_mode_flag(mode_flag::meta_escape))
            {
                enqueue('\033');
                enqueue(val);
            }
            else enqueue(val | 0x80);
        }

        void handle_ascii(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx, kbd);
            if (up || val >= ascii_count)
                return;

            const auto base = val < 10 ? 10 : 16;
            if (val >= 10)
                val -= 10;

            if (!npadch_active)
            {
                npadch_value = 0;
                npadch_active = true;
            }
            npadch_value = npadch_value * base + val;
        }

        void handle_lock(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx);
            if (!up && !rep && val < 8)
                kbd.lock ^= (1 << val);
        }

        void handle_lowercase(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx, kbd, val, up);
        }

        void handle_slock(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            handle_shift(ctx, kbd, val, up);
            if (up || rep || val >= 8)
                return;

            if (!key_maps[kbd.lock ^ kbd.slock])
                kbd.slock = (1 << val);
            else
                kbd.slock ^= (1 << val);
        }

        void handle_dead2(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx);
            if (!up)
                diacr = diacr ? combine_diacr(kbd, val) : val;
        }

        void handle_brl(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            // TODO
            lib::unused(ctx, kbd, val, up);
        }

        void handle_csi(handler_ctx_t &ctx, keyboard_t &kbd, std::uint32_t val, bool up)
        {
            lib::unused(ctx, kbd);
            if (up || val > csi_max)
                return;

            enqueue("\033[");
            if (val >= 10)
                enqueue('0' + val / 10);
            enqueue('0' + val % 10);

            if (const auto modifier = csi_mod(); modifier > 1)
            {
                enqueue(';');
                enqueue('0' + modifier);
            }
            enqueue('~');
        }

        struct kbd_handle_t final : handle_t
        {
            std::weak_ptr<device_t> dev;
            bool revoked = false;

            kbd_handle_t(std::weak_ptr<device_t> device) : dev { std::move(device) } { }
            ~kbd_handle_t() { revoke(); }

            void revoke() override
            {
                const std::unique_lock _ { lock };
                if (std::exchange(revoked, true))
                    return;

                const auto device = dev.lock();
                lib::erase_if(keyboards, [&](const auto &weak) {
                    const auto current = weak.lock();
                    return !current || current == device;
                });
            }

            void receive_raw(keyboard_t &kbd, const value_t &val)
            {
                if (kbd.kbdmode == k_raw)
                    enqueue(val.value);
            }

            void receive_key(handler_ctx_t &ctx, keyboard_t &kbd, bool hw_raw, const value_t &val)
            {
                rep = (val.value == key_repeated);

                bool raw_mode = (kbd.kbdmode == k_raw);
                if (raw_mode && !hw_raw)
                {
                    // emulate raw
                    const std::uint8_t up_flag = (val.value == key_released) << 7;
                    switch (val.code)
                    {
                        case key_pause:
                            enqueue(0xE1);
                            enqueue(0x1D | up_flag);
                            enqueue(0x45 | up_flag);
                            break;
                        case key_hangeul:
                            if (!up_flag)
                                enqueue(0xF2);
                            break;
                        case key_hanja:
                            if (!up_flag)
                                enqueue(0xF1);
                            break;
                        case key_sysrq:
                            if (!keys_down.get(key_leftalt) && !keys_down.get(key_rightalt))
                            {
                                enqueue(0xE0);
                                enqueue(0x2A | up_flag);
                                enqueue(0xE0);
                                enqueue(0x37 | up_flag);
                            }
                            else enqueue(0x54 | up_flag);
                            break;
                        default:
                        {
                            if (val.code > 255)
                                break;

                            const auto code = x86_keycodes[val.code];
                            if (!code)
                                break;

                            if (code & 0x100)
                                enqueue(0xE0);
                            enqueue((code & 0x7F) | up_flag);
                            break;
                        }
                    }
                }

                if (kbd.kbdmode == k_mediumraw)
                {
                    raw_mode = true;
                    if (val.code >= 128)
                    {
                        enqueue((val.value == key_released) << 7);
                        enqueue((val.code >> 7) | (1 << 7));
                        enqueue(val.code | (1 << 7));
                    }
                    else enqueue(val.code | ((val.value == key_released) << 7));
                }

                keys_down.set(val.code, val.value != key_released);

                if (rep && !kbd.get_mode_flag(mode_flag::autorepeat))
                    return;

                const auto shift = (shift_state | kbd.slock) ^ kbd.lock;
                const auto *key_map = key_maps[shift];

                if (shift && val.code < nr_keys && (!key_map || key_map[val.code] == k_hole))
                {
                    const auto type = key_type(key_maps[0][val.code]);
                    if (type >= 0xF0 && (type - 0xF0 == kt_cur || type - 0xF0 == kt_csi))
                        key_map = key_maps[0];
                }

                if (!key_map)
                {
                    compute_shiftstate();
                    kbd.slock = 0;
                    return;
                }

                std::uint16_t sym;
                if (val.code < nr_keys)
                    sym = key_map[val.code];
                else if (val.code >= key_brl_dot1 && val.code <= key_brl_dot8)
                    sym = key_uni(key(kt_brl, val.code - key_brl_dot1 + 1));
                else
                    return;

                auto type = key_type(sym);
                if (type < 0xF0)
                {
                    if (val.value != key_released && !(raw_mode || kbd.kbdmode == k_off))
                        handle_unicode(ctx, kbd, sym, val.value == key_released);
                    return;
                }

                type -= 0xF0;

                if (type == kt_letter)
                {
                    type = kt_latin;
                    if (kbd.get_led_state(k_capslock))
                    {
                        key_map = key_maps[shift ^ (1 << kg_shift)];
                        if (key_map)
                            sym = key_map[val.code];
                    }
                }

                if ((raw_mode || kbd.kbdmode == k_off) && type != kt_spec && type != kt_shift)
                    return;

                constexpr std::array handlers {
                    handle_self,
                    handle_fn,
                    handle_spec,
                    handle_pad,
                    handle_dead,
                    handle_cons,
                    handle_cur,
                    handle_shift,
                    handle_meta,
                    handle_ascii,
                    handle_lock,
                    handle_lowercase,
                    handle_slock,
                    handle_dead2,
                    handle_brl,
                    handle_csi
                };
                handlers[type](ctx, kbd, key_val(sym), val.value == key_released);

                if (type != kt_slock)
                    kbd.slock = 0;
            }

            void receive(
                device_t &dev, const stamp_t &stamp, std::span<const value_t> packet
            ) override
            {
                lib::unused(stamp);

                std::unique_lock guard { lock };
                if (revoked)
                    return;

                const bool hw_raw = is_raw(dev);

                const auto console = vt::active();
                auto kbd = get_keyboard(console);
                lib::bug_on(!kbd);

                handler_ctx_t ctx { console };
                const auto old_leds = kbd->get_led_lights();

                for (const auto &val : packet)
                {
                    if (val.type == ev_msc && val.code == msc_raw && hw_raw)
                        receive_raw(*kbd, val);
                    if (val.type == ev_key && val.code <= key_max)
                        receive_key(ctx, *kbd, hw_raw, val);
                    submit(console);
                }

                const bool leds_changed = old_leds != kbd->get_led_lights();
                guard.unlock();
                if (leds_changed)
                    refresh_leds();

                if (ctx.previous)
                    vt::activate_previous();
                else if (ctx.target)
                    vt::activate(ctx.target);
            }
        };

        struct kbd_consumer_t final : consumer_t
        {
            kbd_consumer_t() : consumer_t { "keyboard" } { }

            bool match(const device_t &dev) override
            {
                if (dev.events.get(ev_snd))
                    return true;

                if (!dev.events.get(ev_key))
                    return false;

                const auto keys = dev.supported.bits(ev_key);
                if (const auto code = keys.find(true, key_reserved); code && *code < btn_misc)
                    return true;

                // if (const auto code = keys.find(true, key_brl_dot1); code && *code <= key_brl_dot10)
                //     return true;

                return false;
            }

            std::shared_ptr<handle_t> connect(device_t &dev) override
            {
                auto device = std::static_pointer_cast<device_t>(dev.as_shared());
                auto handle = std::make_shared<kbd_handle_t>(device);
                {
                    const std::unique_lock _ { lock };
                    keyboards.emplace_back(device);
                }
                refresh_leds();
                return handle;
            }
        } consoomer;

        lib::initgraph::task register_task
        {
            "input.kbd.register",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require { vt::registered_stage() },
            [] {
                if (const auto ret = register_consumer(consoomer); !ret)
                {
                    lib::error(
                        "keyboard: could not register consumer: {}",
                        lib::error_name(ret.error())
                    );
                }
            }
        };
    } // namespace

    std::optional<kbmode> get_mode(std::size_t console)
    {
        const auto kbd = get_keyboard(console);
        if (!kbd)
            return std::nullopt;

        const std::unique_lock _ { lock };
        return static_cast<kbmode>(kbd->kbdmode);
    }

    bool set_mode(std::size_t console, kbmode mode)
    {
        if (!magic_enum::enum_contains(mode))
            return false;

        const auto kbd = get_keyboard(console);
        if (!kbd)
            return false;

        const std::unique_lock _ { lock };
        kbd->kbdmode = mode;
        if (mode == k_xlate || mode == k_unicode)
            compute_shiftstate();
        return true;
    }

    std::optional<bool> get_mode_flag(std::size_t console, mode_flag flag)
    {
        if (flag == mode_flag::none || !magic_enum::enum_contains(flag))
            return std::nullopt;

        const auto kbd = get_keyboard(console);
        if (!kbd)
            return std::nullopt;

        const std::unique_lock _ { lock };
        return kbd->get_mode_flag(flag);
    }

    bool set_mode_flag(std::size_t console, mode_flag flag, bool enabled)
    {
        if (flag == mode_flag::none || !magic_enum::enum_contains(flag))
            return false;

        const auto kbd = get_keyboard(console);
        if (!kbd)
            return false;

        const std::unique_lock _ { lock };
        if (enabled)
            kbd->mode_flags = kbd->mode_flags | flag;
        else
            kbd->mode_flags = kbd->mode_flags & ~flag;
        return true;
    }

    bool reset_mode_flags(std::size_t console)
    {
        const auto kbd = get_keyboard(console);
        if (!kbd)
            return false;

        const std::unique_lock _ { lock };
        kbd->mode_flags = (kbd->mode_flags & mode_flag::meta_escape) | mode_flag::autorepeat;
        return true;
    }

    std::optional<std::uint8_t> get_led_state(std::size_t console)
    {
        const auto kbd = get_keyboard(console);
        if (!kbd)
            return std::nullopt;

        const std::unique_lock _ { lock };
        return kbd->ledstate | (kbd->defledstate << 4);
    }

    bool set_led_state(std::size_t console, std::uint32_t state)
    {
        if (state & ~0x77)
            return false;

        const auto kbd = get_keyboard(console);
        if (!kbd)
            return false;

        {
            const std::unique_lock _ { lock };
            kbd->ledstate = state & 0x07;
            kbd->defledstate = (state >> 4) & 0x07;
        }
        refresh_leds();
        return true;
    }

    bool set_led_lights(std::size_t console, std::uint32_t lights)
    {
        const auto kbd = get_keyboard(console);
        if (!kbd)
            return false;

        {
            const std::unique_lock _ { lock };
            if (!(lights & ~0x07))
            {
                ledioctl = lights;
                kbd->ledmode = 1;
            }
            else kbd->ledmode = 0;
        }
        refresh_leds();
        return true;
    }

    void refresh_leds()
    {
        sched::schedule_work([] {
            std::uint8_t lights = 0;
            std::vector<std::shared_ptr<device_t>> devices;
            {
                const std::unique_lock _ { lock };

                if (const auto kbd = get_keyboard(vt::active()))
                    lights = kbd->get_led_lights();

                lib::erase_if(keyboards, [](const auto &dev) { return dev.expired(); });
                devices.reserve(keyboards.size());
                for (const auto &weak : keyboards)
                {
                    if (auto dev = weak.lock())
                        devices.push_back(std::move(dev));
                }
            }

            const value_t packet[] {
                { ev_led, led_scrolll, !!(lights & k_scrolllock) },
                { ev_led, led_numl, !!(lights & k_numlock) },
                { ev_led, led_capsl, !!(lights & k_capslock) },
                { ev_syn, syn_report, 0 }
            };
            for (const auto &dev : devices)
                dev->report(packet);
        });
    }
} // namespace input::kbd
