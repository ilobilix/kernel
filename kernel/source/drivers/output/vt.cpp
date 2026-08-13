// Copyright (C) 2024-2026  ilobilo

module;

#include <flanterm.h>

module drivers.output.vt;

import drivers.fs.devtmpfs;
import drivers.fs.dev.tty;
import drivers.output;
import system.input;
import system.sched;
import system.vfs;
import system.dev;
import frigg;
import fmt;

namespace output::vt
{
    namespace tty = fs::dev::tty;
    namespace
    {
        constexpr bool debug = false;

        struct vt_mode
        {
            std::uint8_t mode;
            std::uint8_t waitv;
            std::int16_t relsig;
            std::int16_t acqsig;
            std::int16_t frsig;
        };

        struct vt_stat
        {
            std::uint16_t active;
            std::uint16_t signal;
            std::uint16_t state;
        };

        struct vt_t;
        struct slot_t
        {
            flanterm_context *ctx;
            std::weak_ptr<vt_t> inst;
            std::atomic_bool decckm;
        };

        // + 1 cuz tty0
        std::array<slot_t, num_consoles + 1> slots { };
        std::atomic<std::size_t> current = 1;

        sched::mutex_t switch_lock;
        std::size_t pending = 0;
        sched::wait_queue_t switch_wq;

        struct vt_t : tty::instance
        {
            std::atomic<int> mode;
            std::atomic<pid_t> owner;
            lib::locker<vt_mode, sched::mutex_t> vtmode;

            flanterm_context *context() const { return slots[minor].ctx; }

            bool graphics() const
            {
                return mode.load(std::memory_order_acquire) == kd_graphics;
            }

            std::size_t transmit(std::span<std::byte> buffer) override
            {
                if (!graphics())
                {
                    term::write(context(), {
                        reinterpret_cast<const char *>(buffer.data()),
                        buffer.size()
                    });
                }
                return buffer.size_bytes();
            }

            std::size_t can_transmit() override
            {
                return std::numeric_limits<std::size_t>::max();
            }

            lib::expect<void> open(std::shared_ptr<vfs::file_t> file) override
            {
                lib::unused(file);
                return { };
            }

            lib::expect<void> close() override;

            vt_t(tty::driver *drv, std::uint32_t minor)
                : instance { drv, minor, std::make_shared<tty::default_ldisc>(this) },
                  mode { kd_text }, owner { 0 }, vtmode { vt_auto, 0, 0, 0, 0 }
            {
                std::size_t cols = 0, rows = 0;
                term::dimensions(context(), cols, rows);

                if (cols != 0 && rows != 0)
                {
                    auto locked = winsize.lock();
                    locked->ws_col = cols;
                    locked->ws_row = rows;
                }
            }
        };

        std::shared_ptr<vt_t> get_instance(std::size_t index)
        {
            if (index == 0 || index > num_consoles)
                return nullptr;
            return slots[index].inst.lock();
        }

        std::size_t get_slot(const flanterm_context *ctx)
        {
            for (const auto &[index, slot] : slots | std::views::enumerate)
            {
                if (slot.ctx == ctx)
                    return index;
            }
            return 0;
        }

        void reply(std::size_t index, std::string_view str)
        {
            const auto vt = get_instance(index);
            if (!vt)
                return;

            vt->receive({
                const_cast<std::byte *>(reinterpret_cast<const std::byte *>(str.data())),
                str.size()
            });
        }

        void callback(
            flanterm_context *ctx, std::uint64_t type,
            std::uint64_t count, std::uint64_t values, std::uint64_t final
        )
        {
            const auto index = get_slot(ctx);
            if (index == 0)
                return;

            switch (type)
            {
                case FLANTERM_CB_DEC:
                {
                    if (final != 'h' && final != 'l')
                        break;

                    const std::span vals {
                        reinterpret_cast<const std::uint32_t *>(values),
                        count
                    };

                    if (std::ranges::contains(vals, 1u))
                        slots[index].decckm.store(final == 'h', std::memory_order_release);
                    break;
                }
                case FLANTERM_CB_PRIVATE_ID:
                    reply(index, "\e[?1;2c");
                    break;
                case FLANTERM_CB_STATUS_REPORT:
                    reply(index, "\e[0n");
                    break;
                case FLANTERM_CB_POS_REPORT:
                    reply(index, fmt::format("\e[{};{}R", values, count));
                    break;
                case FLANTERM_CB_KBD_LEDS:
                case FLANTERM_CB_OSC:
                case FLANTERM_CB_BELL:
                case FLANTERM_CB_LINUX:
                case FLANTERM_CB_MODE:
                    // TODO
                default:
                    break;
            }
        }

        void signal_owner(const std::shared_ptr<vt_t> &vt, int sig)
        {
            if (!vt || sig <= 0 || sig > sched::nsig)
                return;

            const auto proc = sched::get_process(vt->owner.load(std::memory_order_acquire));
            if (!proc)
                return;

            const sched::siginfo_t info {
                .signo = sig,
                .code = sched::si_kernel,
                .err = 0,
                .pid = 0,
                .uid = 0,
                .status = 0,
                .addr = 0,
                .value = 0
            };
            sched::send_signal(proc.get(), info);
        }

        void commit(std::size_t index)
        {
            pending = 0;

            const auto old = current.load(std::memory_order_acquire);
            if (old == index)
                return;

            term::set_visible(slots[old].ctx, false);

            auto ctx = slots[index].ctx;
            current.store(index, std::memory_order_release);

            const auto vt = get_instance(index);
            term::set_visible(ctx, !(vt && vt->graphics()));

            if (vt)
            {
                const auto vtmode = *vt->vtmode.lock();
                if (vtmode.mode == vt_process)
                    signal_owner(vt, vtmode.acqsig);
            }

            if constexpr (debug)
                lib::debug("vt: switched to vt {}", index);
            switch_wq.wake_all();
        }

        lib::expect<void> request_switch(std::size_t index)
        {
            if (index == 0 || index > num_consoles)
                return std::unexpected { lib::err::invalid_argument };

            if (index == current.load(std::memory_order_acquire))
            {
                pending = 0;
                return { };
            }

            const auto vt = get_instance(current.load(std::memory_order_acquire));
            if (!vt)
            {
                commit(index);
                return { };
            }

            const auto vtmode = *vt->vtmode.lock();
            if (vtmode.mode != vt_process)
            {
                commit(index);
                return { };
            }

            pending = index;
            signal_owner(vt, vtmode.relsig);
            return { };
        }

        void resolve_pending(std::size_t index)
        {
            if (pending != 0 && index == current.load(std::memory_order_acquire))
                commit(pending);
        }

        lib::expect<void> vt_t::close()
        {
            const std::unique_lock _ { switch_lock };

            *vtmode.lock() = { vt_auto, 0, 0, 0, 0 };
            owner.store(0, std::memory_order_release);
            slots[minor].decckm.store(false, std::memory_order_release);

            if (mode.exchange(kd_text, std::memory_order_acq_rel) == kd_graphics &&
                minor == current.load(std::memory_order_acquire))
                term::set_visible(context(), true);

            resolve_pending(minor);
            return { };
        }

        struct driver_t : tty::driver
        {
            std::shared_ptr<tty::instance> create_instance(std::uint32_t minor) override
            {
                if (minor == 0 || minor > num_consoles)
                    return nullptr;

                auto inst = std::make_shared<vt_t>(this, minor);
                slots[minor].inst = inst;
                return inst;
            }

            void destroy_instance(std::shared_ptr<tty::instance> inst) override
            {
                lib::unused(inst);
            }

            lib::expect<int> ioctl(
                tty::instance *inst, std::uint64_t request, lib::uptr_or_addr argp
            ) override
            {
                lib::bug_on(!inst);
                const auto index = inst->minor;
                switch (request)
                {
                    case tty::kdgkbmode:
                    {
                        const auto mode = input::get_kbmode(index);
                        if (!mode)
                            return std::unexpected { mode.error() };
                        const int value = *mode;
                        if (!argp.write(value))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case tty::kdskbmode:
                    {
                        const auto mode = static_cast<input::kbmode>(argp.value());
                        if (const auto ret = input::set_kbmode(index, mode); !ret)
                            return std::unexpected { ret.error() };
                        return 0;
                    }
                    case tty::kdsigaccept:
                    {
                        // TODO: magic keys
                        const int sig = argp.value();
                        if (sig < 1 || sig > sched::nsig || sig == sched::sigkill || sig == sched::sigstop)
                            return std::unexpected { lib::err::invalid_argument };
                        return 0;
                    }
                    case tty::kdgkbled:
                    {
                        // TODO
                        constexpr std::uint8_t no_locks = 0;
                        if (!argp.write(no_locks))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case tty::kdskbled:
                        // TODO
                        return 0;
                    case tty::kdgkbtype:
                        // TODO
                        if (!argp.write(kb_101))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    case tty::kdsetmode:
                    {
                        const int mode = argp.value();
                        if (mode != kd_text && mode != kd_graphics)
                            return std::unexpected { lib::err::invalid_argument };

                        const auto vt = get_instance(index);
                        if (!vt)
                            return std::unexpected { lib::err::no_such_device };

                        const std::unique_lock _ { switch_lock };
                        if (vt->mode.exchange(mode, std::memory_order_acq_rel) == mode)
                            return 0;

                        if (index != current.load(std::memory_order_acquire))
                            return 0;

                        term::set_visible(vt->context(), mode != kd_graphics);
                        return 0;
                    }
                    case tty::kdgetmode:
                    {
                        const auto vt = get_instance(index);
                        const auto mode = vt ? vt->mode.load(std::memory_order_acquire) : kd_text;
                        if (!argp.write(mode))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case tty::vt_getstate:
                    {
                        vt_stat stat { };
                        {
                            const std::unique_lock _ { switch_lock };
                            stat.active = current.load(std::memory_order_acquire);
                            stat.signal = 0;
                            stat.state = 1;
                            for (std::size_t i = 1; i <= num_consoles; i++)
                            {
                                if (slots[i].inst.lock())
                                    stat.state |= 1u << i;
                            }
                        }
                        if (!argp.write(stat))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case tty::vt_openqry:
                    {
                        int free = -1;
                        {
                            const std::unique_lock _ { switch_lock };
                            for (std::size_t i = 1; i <= num_consoles; i++)
                            {
                                if (!slots[i].inst.lock())
                                {
                                    free = i;
                                    break;
                                }
                            }
                        }
                        if (free < 0)
                            return std::unexpected { lib::err::no_space_left };
                        if (!argp.write(free))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case tty::vt_getmode:
                    {
                        const auto vt = get_instance(index);
                        if (!vt)
                            return std::unexpected { lib::err::no_such_device };
                        if (!argp.write(*vt->vtmode.lock()))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case tty::vt_setmode:
                    {
                        vt_mode mode { };
                        if (!argp.read(mode))
                            return std::unexpected { lib::err::invalid_address };

                        if (mode.mode != vt_auto && mode.mode != vt_process)
                            return std::unexpected { lib::err::invalid_argument };

                        const auto valid = [](std::int16_t sig) {
                            return sig >= 0 && sig <= sched::nsig &&
                                sig != sched::sigkill && sig != sched::sigstop;
                        };
                        if (!valid(mode.relsig) || !valid(mode.acqsig) || !valid(mode.frsig))
                            return std::unexpected { lib::err::invalid_argument };

                        const auto vt = get_instance(index);
                        if (!vt)
                            return std::unexpected { lib::err::no_such_device };

                        const std::unique_lock _ { switch_lock };
                        *vt->vtmode.lock() = mode;
                        vt->owner.store(
                            mode.mode == vt_process ? sched::current_process()->pid : 0,
                            std::memory_order_release
                        );

                        if (mode.mode == vt_auto)
                            resolve_pending(index);
                        return 0;
                    }
                    case tty::vt_reldisp:
                    {
                        const int arg = argp.value();

                        const std::unique_lock _ { switch_lock };
                        if (index != current.load(std::memory_order_acquire))
                            return std::unexpected { lib::err::invalid_argument };

                        if (pending == 0)
                        {
                            if (arg == 2) // VT_ACKACQ
                                return 0;
                            return std::unexpected { lib::err::invalid_argument };
                        }

                        if (arg == 0)
                        {
                            pending = 0;
                            return 0;
                        }

                        commit(pending);
                        return 0;
                    }
                    case tty::vt_activate:
                    {
                        const std::size_t target = argp.value();
                        const std::unique_lock _ { switch_lock };
                        if (const auto ret = request_switch(target); !ret)
                            return std::unexpected { ret.error() };
                        return 0;
                    }
                    case tty::vt_waitactive:
                    {
                        const std::size_t target = argp.value();
                        if (target == 0 || target > num_consoles)
                            return std::unexpected { lib::err::invalid_argument };

                        while (true)
                        {
                            const auto gen = switch_wq.snapshot_gen();
                            if (current.load(std::memory_order_acquire) == target)
                                return 0;

                            const auto res = switch_wq.wait_prepared(gen);
                            if (res.interrupted || res.killed)
                                return std::unexpected { lib::err::interrupted };
                        }
                    }
                    case tty::vt_disallocate:
                    {
                        const std::size_t target = argp.value();
                        if (target > num_consoles)
                            return std::unexpected { lib::err::invalid_argument };

                        const std::unique_lock _ { switch_lock };
                        const auto destroy_one = [&](std::size_t i) {
                            if (i == current.load(std::memory_order_acquire) || slots[i].inst.lock())
                                return false;

                            if (i != 1 && slots[i].ctx)
                            {
                                term::destroy(slots[i].ctx);
                                slots[i].ctx = nullptr;
                            }
                            return true;
                        };

                        if (target != 0)
                        {
                            if (!destroy_one(target))
                                return std::unexpected { lib::err::target_is_busy };
                            return 0;
                        }
                        for (std::size_t i = 1; i <= num_consoles; i++)
                            destroy_one(i);
                        return 0;
                    }
                    default:
                        return std::unexpected { lib::err::inappropriate_ioctl };
                }
            }

            driver_t() : tty::driver {
                "vt", "tty", 1,
                4, 1, num_consoles,
                tty::flag::none, tty::type::console, tty::subtype::syscons,
                tty::ktermios::standard()
            } { }
        } driver { };
    } // namespace

    std::size_t active()
    {
        return current.load(std::memory_order_acquire);
    }

    lib::expect<void> activate(std::size_t index)
    {
        const std::unique_lock _ { switch_lock };
        return request_switch(index);
    }

    bool receive_input(std::span<std::byte> buffer)
    {
        const auto vt = get_instance(current.load(std::memory_order_acquire));
        if (!vt)
            return false;
        return vt->receive(buffer);
    }

    bool is_decckm()
    {
        const auto index = current.load(std::memory_order_acquire);
        if (index == 0 || index > num_consoles)
            return false;
        return slots[index].decckm.load(std::memory_order_acquire);
    }

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "output.vt.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task contexts_task
    {
        "output.vt.contexts",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { output::initialised_stage() },
        [] {
            slots[1].ctx = term::main();
            for (std::size_t i = 2; i <= num_consoles; i++)
            {
                slots[i].ctx = term::create();
                if (!slots[i].ctx)
                    lib::warn("vt: could not create a context for vt {}", i);
                term::set_visible(slots[i].ctx, false);
            }
            term::set_visible(slots[1].ctx, true);

            for (auto &slot : slots)
            {
                if (slot.ctx)
                    flanterm_set_callback(slot.ctx, callback);
            }
        }
    };

    lib::initgraph::task register_task
    {
        "output.vt.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            fs::devtmpfs::mounted_stage(),
            ::dev::available_stage()
        },
        lib::initgraph::entail { registered_stage() },
        [] {
            tty::register_driver(&driver);
            tty::register_redirect(
                makedev(4, 0), &driver,
                [] -> std::uint32_t { return active(); }
            );
        }
    };
} // namespace output::vt
