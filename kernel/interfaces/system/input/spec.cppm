// Copyright (C) 2024-2026  ilobilo

export module system.input:spec;

import lib;
import std;

export namespace input
{
    constexpr int ev_version = 0x010001;

    struct value_t
    {
        std::uint16_t type;
        std::uint16_t code;
        std::int32_t value;
    };

    struct event_t
    {
        timeval time;
        std::uint16_t type;
        std::uint16_t code;
        std::int32_t value;
    };

    struct id_t
    {
        std::uint16_t bustype;
        std::uint16_t vendor;
        std::uint16_t product;
        std::uint16_t version;
    };

    struct absinfo_t
    {
        std::int32_t value;
        std::int32_t minimum;
        std::int32_t maximum;
        std::int32_t fuzz;
        std::int32_t flat;
        std::int32_t resolution;
    };

    constexpr std::uint8_t keymap_by_index = (1 << 0);
    struct keymap_entry_t
    {
        std::uint8_t flags;
        std::uint8_t len;
        std::uint16_t index;
        std::uint32_t keycode;
        std::uint8_t scancode[32];
    };

    struct mask_t
    {
        std::uint32_t type;
        std::uint32_t codes_size;
        std::uint64_t codes_ptr;
    };

    struct ff_replay
    {
        std::uint16_t length;
        std::uint16_t delay;
    };

    struct ff_trigger
    {
        std::uint16_t button;
        std::uint16_t interval;
    };

    struct ff_envelope
    {
        std::uint16_t attack_length;
        std::uint16_t attack_level;
        std::uint16_t fade_length;
        std::uint16_t fade_level;
    };

    struct ff_constant_effect
    {
        std::int16_t level;
        ff_envelope envelope;
    };

    struct ff_ramp_effect
    {
        std::int16_t start_level;
        std::int16_t end_level;
        ff_envelope envelope;
    };

    struct ff_condition_effect
    {
        std::uint16_t right_saturation;
        std::uint16_t left_saturation;

        std::int16_t right_coeff;
        std::int16_t left_coeff;

        std::uint16_t deadband;
        std::int16_t center;
    };

    struct ff_periodic_effect
    {
        std::uint16_t waveform;
        std::uint16_t period;
        std::int16_t magnitude;
        std::int16_t offset;
        std::uint16_t phase;

        ff_envelope envelope;

        std::uint32_t custom_len;
        std::int16_t __user *custom_data;
    };

    struct ff_rumble_effect
    {
        std::uint16_t strong_magnitude;
        std::uint16_t weak_magnitude;
    };

    struct ff_haptic_effect
    {
        std::uint16_t hid_usage;
        std::uint16_t vendor_id;
        std::uint8_t  vendor_waveform_page;
        std::uint16_t intensity;
        std::uint16_t repeat_count;
        std::uint16_t retrigger_period;
    };

    struct ff_effect
    {
        std::uint16_t type;
        std::int16_t id;
        std::uint16_t direction;
        ff_trigger trigger;
        ff_replay replay;

        union {
            ff_constant_effect constant;
            ff_ramp_effect ramp;
            ff_periodic_effect periodic;
            ff_condition_effect condition[2]; // One for each axis
            ff_rumble_effect rumble;
            ff_haptic_effect haptic;
        } data;
    };

    enum id : std::uint8_t
    {
        id_bus = 0,
        id_vendor = 1,
        id_product = 2,
        id_version = 3
    };

    enum bus : std::uint16_t
    {
        bus_pci = 0x01,
        bus_isapnp = 0x02,
        bus_usb = 0x03,
        bus_hil = 0x04,
        bus_bluetooth = 0x05,
        bus_virtual = 0x06,

        bus_isa = 0x10,
        bus_i8042 = 0x11,
        bus_xtkbd = 0x12,
        bus_rs232 = 0x13,
        bus_gameport = 0x14,
        bus_parport = 0x15,
        bus_amiga = 0x16,
        bus_adb = 0x17,
        bus_i2c = 0x18,
        bus_host = 0x19,
        bus_gsc = 0x1A,
        bus_atari = 0x1B,
        bus_spi = 0x1C,
        bus_rmi = 0x1D,
        bus_cec = 0x1E,
        bus_intel_ishtp = 0x1F,
        bus_amd_sfh = 0x20,
        bus_sdw = 0x21
    };

    enum mt_tool : std::int32_t
    {
        mt_tool_finger = 0x00,
        mt_tool_pen = 0x01,
        mt_tool_palm = 0x02,
        mt_tool_dial = 0x0A,
        mt_tool_max = 0x0F
    };

    enum ff_status : std::int32_t
    {
        ff_status_stopped = 0x00,
        ff_status_playing = 0x01,
        ff_status_max = 0x01
    };

    enum ff_effects : std::uint16_t
    {
        ff_haptic = 0x4F,
        ff_rumble = 0x50,
        ff_periodic = 0x51,
        ff_constant = 0x52,
        ff_spring = 0x53,
        ff_friction = 0x54,
        ff_damper = 0x55,
        ff_inertia = 0x56,
        ff_ramp = 0x57,

        ff_effect_min = ff_haptic,
        ff_effect_max = ff_ramp,

        ff_square = 0x58,
        ff_triangle = 0x59,
        ff_sine = 0x5A,
        ff_saw_up = 0x5B,
        ff_saw_down = 0x5C,
        ff_custom = 0x5D,

        ff_waveform_min = ff_square,
        ff_waveform_max = ff_custom,

        ff_gain = 0x60,
        ff_autocenter = 0x61,

        ff_max_effects = ff_gain,

        ff_max = 0x7F,
        ff_cnt = (ff_max + 1)
    };

    enum kbmode
    {
        k_raw = 0x00,
        k_xlate = 0x01,
        k_mediumraw = 0x02,
        k_unicode = 0x03,
        k_off = 0x04
    };

    using namespace lib::ioc;
    enum ioctls : std::uint32_t
    {
        eviocgversion = make_ior<int>('E', 0x01), // get driver version
        eviocgid = make_ior<id_t>('E', 0x02),     // get device ID
        eviocgrep = make_ior<std::uint32_t [2]>('E', 0x03), // get repeat settings
        eviocsrep = make_iow<std::uint32_t [2]>('E', 0x03), // set repeat settings

        eviocgkeycode = make_ior<std::uint32_t [2]>('E', 0x04), // get keycode
        eviocgkeycode_v2 = make_ior<keymap_entry_t>('E', 0x04),
        eviocskeycode = make_iow<std::uint32_t [2]>('E', 0x04), // set keycode
        eviocskeycode_v2 = make_iow<keymap_entry_t>('E', 0x04),

        eviocsff = make_iow<ff_effect>('E', 0x80), // send a force effect to a force feedback device
        eviocrmff = make_iow<int>('E', 0x81),      // Erase a force effect
        eviocgeffects = make_ior<int>('E', 0x84),  // Report number of effects playable at the same time

        eviocgrab = make_iow<int>('E', 0x90),   // Grab/Release device
        eviocrevoke = make_iow<int>('E', 0x91), // Revoke device access

        eviocgmask = make_ior<mask_t>('E', 0x92), // Get event-masks
        eviocsmask = make_iow<mask_t>('E', 0x93), // Set event-masks

        eviocsclockid = make_iow<int>('E', 0xA0) // Set clockid to be used for timestamps
    };

    // get device name
    consteval std::uint32_t eviocgname(std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x06, len);
    }

    // get physical location
    consteval std::uint32_t eviocgphys(std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x07, len);
    }

    // get unique identifier
    consteval std::uint32_t eviocguniq(std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x08, len);
    }

    // get device properties
    consteval std::uint32_t eviocgprop(std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x09, len);
    }

    // get MT slot values
    consteval std::uint32_t eviocgmtslots(std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x0A, len);
    }

    // get global key state
    consteval std::uint32_t eviocgkey(std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x18, len);
    }

    // get all LEDs
    consteval std::uint32_t eviocgled(std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x19, len);
    }

    // get all sounds status
    consteval std::uint32_t eviocgsnd(std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x1A, len);
    }

    // get all switch states
    consteval std::uint32_t eviocgsw(std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x1B, len);
    }

    // get event bits
    consteval std::uint32_t eviocgbit(std::uint32_t ev, std::uint32_t len)
    {
        return make_ioc(read, 'E', 0x20 + ev, len);
    }

    // get abs value/limits
    consteval std::uint32_t eviocgabs(std::uint32_t abs)
    {
        return make_ior<absinfo_t>('E', 0x40 + abs);
    }

    // set abs value/limits
    consteval std::uint32_t eviocsabs(std::uint32_t abs)
    {
        return make_iow<absinfo_t>('E', 0xC0 + abs);
    }
} // export namespace input
