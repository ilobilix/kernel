// Copyright (C) 2024-2026  ilobilo

module system.input;

import system.cpu;
import system.vfs;
import frigg;
import fmt;

namespace input
{
    namespace
    {
        constexpr std::uint32_t major = 13;
        constexpr std::uint32_t first_minor = 64;
        lib::static_bitmap<32> minors;

        lib::expect<std::uint32_t> alloc_minor()
        {
            if (const auto minor = minors.atomic_view().allocate(0, std::memory_order_acq_rel))
                return *minor + first_minor;
            return std::unexpected { lib::err::no_space_left };
        }

        void free_minor(std::uint32_t minor)
        {
            lib::bug_on(!minors.atomic_view().set(
                minor - first_minor, false, std::memory_order_release
            ));
        }

        struct evdev_ktype_t final : dev::ktype_t
        {
            std::span<dev::attribute_t *const> attributes() const override
            {
                static dev::attribute_t *list[] {
                    dev::dev_attribute()
                };
                return list;
            }
        };

        dev::ktype_t &evdev_ktype()
        {
            static evdev_ktype_t type { };
            return type;
        }

        struct evdev_t;
        struct evdev_consumer_t final : consumer_t
        {
            evdev_consumer_t() : consumer_t { "evdev" } { }

            bool match(const device_t &dev) override
            {
                lib::unused(dev);
                return true;
            }

            std::shared_ptr<handle_t> connect(device_t &dev) override;

            bool always_open() const override { return false; }
        } consumer;

        struct client_t;
        struct client_deleter_t
        {
            void operator()(client_t *client) const;
        };

        // open file
        struct client_t : std::enable_shared_from_this<client_t>,
            rcu::obj_base<client_t, client_deleter_t>
        {
            sched::wait_queue_t wait;
            std::weak_ptr<device_t> dev;
            std::atomic_bool revoked;

            std::array<lib::bitmap, ev_cnt> evmasks;

            lib::buffer<event_t> buffer;
            std::size_t head;
            std::size_t tail;
            std::size_t packet;

            chrono::type clkid;

            lib::spinlock_irq lock;

            std::atomic_bool queued;
            client_t *next_pending;
            std::shared_ptr<client_t> self_ref;
            std::shared_ptr<client_t> retire_ref;

            client_t(const std::shared_ptr<device_t> &device)
                : wait { }, dev { device }, revoked { false }, evmasks { },
                  buffer { std::bit_ceil(std::max(device->get_ev_per_packet() * 8, 64uz)) },
                  head { 0 }, tail { 0 }, packet { 0 }, clkid { chrono::realtime }, lock { },
                  queued { false }, next_pending { nullptr }, self_ref { }, retire_ref { } { }

            static std::size_t mask_count(std::uint32_t type)
            {
                if (type == ev_syn)
                    return ev_cnt;
                if (type == ev_rep)
                    return 0;
                return code_count(type);
            }

            bool filtered(std::uint16_t type, std::uint16_t code) const
            {
                if (type == ev_syn || type >= ev_cnt)
                    return false;

                if (const auto &types = evmasks[ev_syn]; types.size() != 0 && !types.get(type))
                    return true;

                const auto count = mask_count(type);
                if (count == 0 || code >= count)
                    return false;

                const auto &mask = evmasks[type];
                return mask.size() != 0 && !mask.get(code);
            }

            void receive(const event_t &record)
            {
                const auto mask = buffer.size() - 1;

                buffer.data()[head] = record;
                head = (head + 1) & mask;

                if (head == tail)
                {
                    tail = (head - 2) & mask;
                    buffer.data()[tail] = { record.time, ev_syn, syn_dropped, 0 };
                    packet = tail;
                }

                if (record.type == ev_syn && record.code == syn_report)
                    packet = head;
            }

            void queue_wake();
            void revoke()
            {
                revoked.store(true, std::memory_order_release);
                queue_wake();
            }

            void receive_values(const std::array<timeval, 3> &times, std::span<const value_t> data)
            {
                if (revoked.load(std::memory_order_acquire))
                    return;

                bool wakeup = false;
                {
                    const std::unique_lock _ { lock };
                    const auto time = times[std::min<std::size_t>(clkid, 2)];

                    for (const auto &record : data)
                    {
                        if (filtered(record.type, record.code))
                            continue;

                        if (record.type == ev_syn && record.code == syn_report)
                        {
                            if (packet == head)
                                continue;
                            wakeup = true;
                        }
                        receive({ time, record.type, record.code, record.value });
                    }
                }

                if (wakeup)
                    queue_wake();
            }
        };

        void client_deleter_t::operator()(client_t *client) const
        {
            const auto _ = std::move(client->retire_ref);
        }

        // eventN
        struct evdev_t final : handle_t
        {
            const std::size_t index;
            std::weak_ptr<device_t> dev;
            std::shared_ptr<dev::device_t> node;

            sched::mutex_t lock;
            rcu::owner<rcu::box<std::vector<client_t *>>> clients;
            rcu::pointer<client_t> grabbed;

            evdev_t(std::weak_ptr<device_t> dev, std::size_t index)
                : index { index }, dev { std::move(dev) }, node { },
                  lock { }, clients { }, grabbed { } { }

            ~evdev_t()
            {
                if (node)
                    dev::unregister_device(std::move(node));
                free_minor(index + first_minor);
            }

            void add_client(client_t *client)
            {
                const std::unique_lock _ { lock };
                rcu::updater next { clients };
                next->push_back(client);
                next.commit();
            }

            void remove_client(client_t *client)
            {
                bool was_grabbed = false;
                {
                    const std::unique_lock _ { lock };
                    if (grabbed.unsafe_load() == client)
                    {
                        grabbed.assign(nullptr);
                        was_grabbed = true;
                    }

                    rcu::updater next { clients };
                    if (lib::erase(*next, client))
                        next.commit();
                }

                client->retire_ref = client->shared_from_this();
                client->retire();

                if (was_grabbed)
                {
                    if (const auto device = dev.lock())
                        lib::unused(device->ungrab(this));
                }
            }

            lib::expect<void> grab(client_t *client)
            {
                const auto device = dev.lock();
                if (!device)
                    return std::unexpected { lib::err::no_such_device };

                const std::unique_lock _ { lock };
                if (const auto ret = device->grab(this); !ret)
                    return ret;

                grabbed.assign(client);
                return { };
            }

            lib::expect<void> ungrab(client_t *client)
            {
                {
                    const std::unique_lock _ { lock };
                    if (grabbed.unsafe_load() != client)
                        return std::unexpected { lib::err::invalid_argument };

                    grabbed.assign(nullptr);
                }
                rcu::synchronise();

                if (const auto device = dev.lock())
                    lib::unused(device->ungrab(this));
                return { };
            }

            void revoke() override
            {
                const rcu::read_guard _ { };
                if (const auto list = clients.dereference())
                {
                    for (const auto client : *list)
                        client->revoke();
                }
            }

            void receive(
                device_t &self, const stamp_t &stamp, std::span<const value_t> packet
            ) override
            {
                lib::unused(self);

                const std::array times {
                    stamp.time(chrono::realtime),
                    stamp.time(chrono::monotonic),
                    stamp.time(chrono::boottime)
                };

                const rcu::read_guard _ { };
                if (const auto client = grabbed.dereference())
                    client->receive_values(times, packet);
                else if (const auto list = clients.dereference())
                {
                    for (const auto client : *list)
                        client->receive_values(times, packet);
                }
            }
        };

        struct ops_t : vfs::ops_t
        {
            std::weak_ptr<evdev_t> _evdev;
            ops_t(std::weak_ptr<evdev_t> evdev) : _evdev { std::move(evdev) } { }

            bool seekable() const override { return false; }

            lib::expect<void> open(std::shared_ptr<vfs::file_t> file, int flags, pid_t pid) override
            {
                lib::unused(flags, pid);

                auto evdev = _evdev.lock();
                if (!evdev)
                    return std::unexpected { lib::err::no_such_device };

                auto dev = evdev->dev.lock();
                if (!dev)
                    return std::unexpected { lib::err::no_such_device };

                if (const auto ret = dev->open_device(evdev.get()); !ret)
                    return ret;

                auto handle = std::make_shared<client_t>(dev);
                evdev->add_client(handle.get());

                file->private_data = std::move(handle);
                return { };
            }

            lib::expect<void> close(vfs::file_t &file) override
            {
                lib::bug_on(!file.private_data);
                const auto handle = std::static_pointer_cast<client_t>(file.private_data);
                if (const auto evdev = _evdev.lock())
                {
                    evdev->remove_client(handle.get());
                    if (const auto dev = evdev->dev.lock())
                        dev->close_device(evdev.get());
                }
                file.private_data.reset();
                return { };
            }

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);
                lib::bug_on(!file || !file->private_data);
                const auto handle = std::static_pointer_cast<client_t>(file->private_data);

                if (buffer.size() != 0 && buffer.size() < sizeof(event_t))
                    return std::unexpected { lib::err::invalid_argument };

                const auto num = buffer.size() / sizeof(event_t);
                std::size_t count = 0;

                while (true)
                {
                    if (handle->revoked.load(std::memory_order_acquire) || handle->dev.expired())
                        return std::unexpected { lib::err::no_such_device };

                    while (count < num)
                    {
                        std::array<event_t, 32> chunk;
                        std::size_t got = 0;
                        {
                            const std::unique_lock _ { handle->lock };
                            const auto mask = handle->buffer.size() - 1;

                            while (got < chunk.size() && count + got < num &&
                                   handle->tail != handle->packet)
                            {
                                chunk[got++] = handle->buffer.data()[handle->tail];
                                handle->tail = (handle->tail + 1) & mask;
                            }
                        }
                        if (got == 0)
                            break;

                        if (!buffer.subspan(count * sizeof(event_t), got * sizeof(event_t))
                            .copy_from(std::as_bytes(std::span { chunk.data(), got })))
                            return std::unexpected { lib::err::invalid_address };

                        count += got;
                    }

                    if (count != 0)
                        break;

                    sched::gen_t gen;
                    {
                        const std::unique_lock _ { handle->lock };
                        if (handle->tail != handle->packet)
                        {
                            if (num == 0)
                                break;
                            continue;
                        }

                        if (file->flags & vfs::o_nonblock)
                            return std::unexpected { lib::err::try_again };

                        if (num == 0)
                            break;
                        gen = handle->wait.snapshot_gen();
                    }

                    const auto res = handle->wait.wait_prepared(gen);
                    if (res.interrupted || res.killed)
                        return std::unexpected { lib::err::interrupted };
                }
                return count * sizeof(event_t);
            }

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(offset);
                if (buffer.size() == 0)
                    return 0;

                if (buffer.size() < sizeof(event_t))
                    return std::unexpected { lib::err::invalid_argument };

                lib::bug_on(!file || !file->private_data);
                const auto handle = std::static_pointer_cast<client_t>(file->private_data);

                if (handle->revoked.load(std::memory_order_acquire))
                    return std::unexpected { lib::err::no_such_device };

                auto dev = handle->dev.lock();
                auto evdev = _evdev.lock();
                if (!dev || !evdev)
                    return std::unexpected { lib::err::no_such_device };

                const auto num = buffer.size() / sizeof(event_t);
                for (std::size_t i = 0; i < num; i++)
                {
                    event_t ev;
                    if (!buffer.subspan(i * sizeof(event_t), sizeof(event_t))
                        .copy_to(std::as_writable_bytes(std::span { &ev, 1 })))
                        return std::unexpected { lib::err::invalid_address };

                    lib::unused(dev->inject(evdev.get(), ev.type, ev.code, ev.value));
                }
                return num * sizeof(event_t);
            }

            lib::expect<int> ioctl(
                std::shared_ptr<vfs::file_t> file, std::uint64_t request,
                lib::uptr_or_addr argp
            ) override
            {
                lib::bug_on(!file || !file->private_data);
                const auto handle = std::static_pointer_cast<client_t>(file->private_data);

                if (handle->revoked.load(std::memory_order_acquire))
                    return std::unexpected { lib::err::no_such_device };

                auto dev = handle->dev.lock();
                if (!dev)
                    return std::unexpected { lib::err::no_such_device };

                const auto out_at = [](std::uintptr_t addr, std::size_t len) {
                    return lib::maybe_uspan<std::byte>::create(
                        reinterpret_cast<void __user *>(addr), len
                    );
                };

                constexpr auto max_bits = lib::div_roundup<std::size_t>(key_cnt, 64) * 8;
                const auto write_bits = [&](
                    lib::uptr_or_addr ptr, const lib::const_bitmap_view bits, std::size_t max
                ) -> lib::expect<int> {
                    const auto full = lib::div_roundup(bits.size(), 64uz) * 8;
                    lib::bug_on(full > max_bits);

                    std::array<std::uint8_t, max_bits> data { };
                    std::memcpy(data.data(), bits.data(), bits.size_bytes());

                    const auto len = std::min(full, max);
                    const auto out = out_at(ptr.value(), len);
                    if (!out || !out->copy_from(std::as_bytes(std::span { data.data(), len })))
                        return std::unexpected { lib::err::invalid_address };
                    return len;
                };

                const auto write_str = [&](
                    lib::uptr_or_addr ptr, const std::string &str, std::size_t max
                ) -> lib::expect<int> {
                    if (str.empty())
                        return std::unexpected { lib::err::not_found };

                    const auto len = std::min(str.size() + 1, max);
                    const auto out = out_at(ptr.value(), len);
                    if (!out || !out->copy_from(std::as_bytes(std::span { str.c_str(), len })))
                        return std::unexpected { lib::err::invalid_address };
                    return len;
                };

                switch (request)
                {
                    case eviocgversion:
                        if (!argp.write(ev_version))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    case eviocgid:
                        if (!argp.write(dev->ident))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    case eviocgrep:
                    {
                        if (!dev->events.get(ev_rep))
                            return std::unexpected { lib::err::not_implemented };

                        const auto rep = dev->get_repeat();
                        const auto val = static_cast<std::uint64_t>(rep[rep_delay]) |
                            (static_cast<std::uint64_t>(rep[rep_period]) << 32);

                        if (!argp.write(val))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case eviocsrep:
                    {
                        if (!dev->events.get(ev_rep))
                            return std::unexpected { lib::err::not_implemented };

                        std::uint64_t val;
                        if (!argp.read(val))
                            return std::unexpected { lib::err::invalid_address };

                        const auto evdev = _evdev.lock();
                        if (!evdev)
                            return std::unexpected { lib::err::no_such_device };

                        const std::int32_t delay = val & 0xFFFFFFFF;
                        lib::unused(dev->inject(evdev.get(), ev_rep, rep_delay, delay));

                        const std::int32_t period = (val >> 32) & 0xFFFFFFFF;
                        lib::unused(dev->inject(evdev.get(), ev_rep, rep_period, period));
                        return 0;
                    }
                    case eviocgkeycode:
                    {
                        keymap_entry_t ke {
                            .flags = 0,
                            .len = sizeof(std::uint32_t),
                            .index = 0,
                            .keycode = 0,
                            .scancode = { }
                        };

                        std::uint32_t val;
                        if (!argp.read(val))
                            return std::unexpected { lib::err::invalid_address };
                        std::memcpy(ke.scancode, &val, sizeof(val));

                        if (const auto ret = dev->get_keycode(ke); !ret)
                            return std::unexpected { ret.error() };

                        if (!argp.add(sizeof(std::uint32_t)).write(ke.keycode))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case eviocgkeycode_v2:
                    {
                        keymap_entry_t ke;
                        if (!argp.read(ke))
                            return std::unexpected { lib::err::invalid_address };

                        if (const auto ret = dev->get_keycode(ke); !ret)
                            return std::unexpected { ret.error() };

                        if (!argp.write(ke))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case eviocskeycode:
                    {
                        keymap_entry_t ke {
                            .flags = 0,
                            .len = sizeof(std::uint32_t),
                            .index = 0,
                            .keycode = 0,
                            .scancode = { }
                        };

                        std::uint32_t val;
                        if (!argp.read(val))
                            return std::unexpected { lib::err::invalid_address };
                        std::memcpy(ke.scancode, &val, sizeof(val));

                        if (!argp.add(sizeof(val)).read(ke.keycode))
                            return std::unexpected { lib::err::invalid_address };

                        if (const auto ret = dev->set_keycode(ke); !ret)
                            return std::unexpected { ret.error() };
                        return 0;
                    }
                    case eviocskeycode_v2:
                    {
                        keymap_entry_t ke;
                        if (!argp.read(ke))
                            return std::unexpected { lib::err::invalid_address };

                        if (ke.len > sizeof(ke.scancode))
                            return std::unexpected { lib::err::invalid_argument };

                        if (const auto ret = dev->set_keycode(ke); !ret)
                            return std::unexpected { ret.error() };
                        return 0;
                    }
                    // TODO
                    // case eviocrmff:
                    // case eviocgeffects:
                    // {
                    //     const std::uint32_t val = dev->events.get(ev_ff) ? dev->ff->max_effects : 0;
                    //     if (!argp.write(val))
                    //         return std::unexpected { lib::err::invalid_address };
                    //     return 0;
                    // }
                    case eviocgrab:
                    {
                        const auto evdev = _evdev.lock();
                        if (!evdev)
                            return std::unexpected { lib::err::no_such_device };

                        const auto do_it = [&] {
                            if (argp.value())
                                return evdev->grab(handle.get());
                            return evdev->ungrab(handle.get());
                        };
                        if (const auto ret = do_it(); !ret)
                            return std::unexpected { ret.error() };
                        return 0;
                    }
                    case eviocrevoke:
                    {
                        if (argp.value())
                            return std::unexpected { lib::err::invalid_argument };

                        if (const auto evdev = _evdev.lock())
                            lib::unused(evdev->ungrab(handle.get()));

                        handle->revoke();
                        return 0;
                    }
                    case eviocgmask:
                    {
                        mask_t req;
                        if (!argp.read(req))
                            return std::unexpected { lib::err::invalid_address };

                        const auto xfer = std::min<std::size_t>(
                            req.codes_size,
                            lib::div_roundup(client_t::mask_count(req.type), 8uz)
                        );
                        if (xfer == 0)
                            return 0;

                        const auto out = out_at(req.codes_ptr, xfer);
                        if (!out)
                            return std::unexpected { lib::err::invalid_address };

                        constexpr auto num = lib::div_roundup<std::size_t>(key_cnt, 8);
                        std::array<std::uint8_t, num> data { };
                        bool set = false;
                        {
                            const std::unique_lock _ { handle->lock };
                            if (req.type < ev_cnt)
                            {
                                if (const auto &mask = handle->evmasks[req.type]; mask.size() != 0)
                                {
                                    std::memcpy(data.data(), mask.data(), xfer);
                                    set = true;
                                }
                            }
                        }

                        const auto ret = set
                            ? out->copy_from(std::as_bytes(std::span { data.data(), xfer }))
                            : out->fill(0xFF);

                        if (!ret)
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case eviocsmask:
                    {
                        mask_t req;
                        if (!argp.read(req))
                            return std::unexpected { lib::err::invalid_address };

                        const auto cnt = client_t::mask_count(req.type);
                        if (cnt == 0)
                            return 0;

                        lib::bitmap mask { cnt };
                        const auto len = std::min<std::size_t>(req.codes_size, mask.size_bytes());

                        const auto in = out_at(req.codes_ptr, len);
                        if (!in)
                            return std::unexpected { lib::err::invalid_address };

                        if (len != 0 &&
                            !in->copy_to(std::as_writable_bytes(std::span { mask.data(), len })))
                            return std::unexpected { lib::err::invalid_address };

                        using std::swap;
                        const std::unique_lock _ { handle->lock };
                        swap(handle->evmasks[req.type], mask);
                        return 0;
                    }
                    case eviocsclockid:
                    {
                        int id;
                        if (!argp.read(id))
                            return std::unexpected { lib::err::invalid_address };

                        using namespace chrono;
                        if (id != realtime && id != monotonic && id != boottime)
                            return std::unexpected { lib::err::invalid_argument };

                        const std::unique_lock _ { handle->lock };
                        handle->clkid = static_cast<type>(id);
                        return 0;
                    }
                }

                const std::size_t size = lib::ioc::make_size(request);
                switch (request & ~lib::ioc::size_mask)
                {
                    case eviocgprop(0):
                        return write_bits(argp, dev->props, size);
                    case eviocgname(0):
                        return write_str(argp, dev->desc, size);
                    case eviocgphys(0):
                        return write_str(argp, dev->phys, size);
                    case eviocguniq(0):
                        return write_str(argp, dev->uniq, size);
                    case eviocgkey(0):
                    case eviocgled(0):
                    case eviocgsnd(0):
                    case eviocgsw(0):
                    {
                        const auto nr = lib::ioc::make_nr(request);
                        const std::uint16_t type =
                            nr == lib::ioc::make_nr(eviocgkey(0)) ? ev_key :
                            nr == lib::ioc::make_nr(eviocgled(0)) ? ev_led :
                            nr == lib::ioc::make_nr(eviocgsnd(0)) ? ev_snd : ev_sw;

                        const auto cnt = code_count(type);
                        lib::static_bitmap<key_cnt> state;
                        dev->snapshot(type, state);

                        return write_bits(argp, { state.data(), cnt }, size);
                    }
                    case eviocgmtslots(0):
                    {
                        const auto count = dev->mt_slots();
                        if (count == 0 || size < sizeof(std::uint32_t))
                            return std::unexpected { lib::err::invalid_argument };

                        std::uint32_t code;
                        if (!argp.read(code))
                            return std::unexpected { lib::err::invalid_address };

                        if (!is_mt_value(code))
                            return std::unexpected { lib::err::invalid_argument };

                        const auto room = std::min(
                            (size - sizeof(std::uint32_t)) / sizeof(std::int32_t), count
                        );
                        if (room == 0)
                            return 0;

                        std::array<std::int32_t, 32> values;
                        for (std::size_t done = 0; done < room; )
                        {
                            const auto got = dev->mt_values(
                                code, { values.data(), std::min(values.size(), room - done) }, done
                            );
                            if (got == 0)
                                break;

                            for (std::size_t i = 0; i < got; i++)
                            {
                                if (!argp.add((done + i + 1) * sizeof(std::int32_t))
                                    .write(values[i]))
                                    return std::unexpected { lib::err::invalid_address };
                            }
                            done += got;
                        }
                        return 0;
                    }
                    // TODO
                    // case eviocsff:
                }

                // Multi-number variable-length handlers
                if (lib::ioc::make_type(request) != 'E')
                    return std::unexpected { lib::err::inappropriate_ioctl };

                const auto nr = lib::ioc::make_nr(request);
                const auto dir = lib::ioc::make_dir(request);

                constexpr std::uint32_t uev_mask = ev_max;
                constexpr std::uint32_t uabs_mask = abs_max;

                if (dir == lib::ioc::read)
                {
                    if ((nr & ~uev_mask) == lib::ioc::make_nr(eviocgbit(0, 0)))
                    {
                        const std::uint16_t type = nr & ev_max;
                        if (type == ev_rep)
                            return std::unexpected { lib::err::invalid_argument };

                        const auto bits = type == 0
                            ? lib::bitmap_view { dev->events }
                            : dev->supported.bits(type);

                        if (bits.size() == 0)
                            return std::unexpected { lib::err::invalid_argument };

                        return write_bits(argp, bits, size);
                    }

                    if ((nr & ~uabs_mask) == lib::ioc::make_nr(eviocgabs(0)))
                    {
                        const auto copy = dev->get_abs(nr & abs_max);
                        if (!copy)
                            return std::unexpected { lib::err::invalid_argument };

                        const auto len = std::min(size, sizeof(*copy));
                        const auto out = out_at(argp.value(), len);

                        const auto span = std::as_bytes(std::span { std::addressof(*copy), 1 });
                        if (!out || !out->copy_from(span.first(len)))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                }

                if (dir == lib::ioc::write && (nr & ~uabs_mask) == lib::ioc::make_nr(eviocsabs(0)))
                {
                    auto info = dev->get_abs(nr & abs_max);
                    if (!info)
                        return std::unexpected { lib::err::invalid_argument };

                    const auto len = std::min(size, sizeof(*info));
                    const auto in = out_at(argp.value(), len);

                    const auto span = std::as_writable_bytes(
                        std::span { std::addressof(*info), 1 }
                    ).first(len);
                    if (!in || !in->copy_to(span))
                        return std::unexpected { lib::err::invalid_address };

                    if (size < sizeof(absinfo_t))
                        info->resolution = 0;

                    if ((nr & abs_max) == abs_mt_slot)
                        return std::unexpected { lib::err::invalid_argument };

                    if (const auto ret = dev->set_absinfo(nr & abs_max, *info); !ret)
                        return std::unexpected { ret.error() };
                    return 0;
                }

                return std::unexpected { lib::err::invalid_argument };
            }

            lib::expect<std::uint16_t> poll(
                std::shared_ptr<vfs::file_t> file, vfs::poll_table_t *pt
            ) override
            {
                lib::bug_on(!file || !file->private_data);
                const auto handle = std::static_pointer_cast<client_t>(file->private_data);

                if (pt)
                    pt->add(handle->wait);

                std::uint16_t events = vfs::pollout | vfs::pollwrnorm;
                if (handle->revoked.load(std::memory_order_acquire) || handle->dev.expired())
                    events = vfs::pollhup | vfs::pollerr;

                const std::unique_lock _ { handle->lock };
                if (handle->packet != handle->tail)
                    events |= vfs::pollin | vfs::pollrdnorm;

                return events;
            }
        };

        std::shared_ptr<handle_t> evdev_consumer_t::connect(device_t &dev)
        {
            const auto minor = alloc_minor();
            if (!minor)
            {
                lib::error("evdev: out of minors for '{}'", dev.name);
                return nullptr;
            }

            const auto index = *minor - first_minor;

            auto self = std::static_pointer_cast<device_t>(dev.as_shared());
            auto evdev = std::make_shared<evdev_t>(self, index);

            auto node = dev::device_t::create(fmt::format("event{}", index), evdev_ktype(), self);
            node->cls = std::addressof(get_class());
            node->devt = makedev(major, *minor);
            node->fops = std::make_shared<ops_t>(evdev);

            if (const auto ret = dev::register_device(node); !ret)
            {
                lib::error("evdev: could not register event{}", index);
                return nullptr;
            }

            evdev->node = std::move(node);
            return evdev;
        }

        // TODO
        lib::spinlock_irq wake_lock;
        client_t *wake_head = nullptr;
        frg::manual_box<sched::irq_worker_t> wake_worker;

        void drain()
        {
            auto list = [] {
                const std::unique_lock _ { wake_lock };
                return std::exchange(wake_head, nullptr);
            } ();

            while (list)
            {
                const auto next = list->next_pending;
                auto self = std::move(list->self_ref);

                list->next_pending = nullptr;
                list->queued.store(false, std::memory_order_release);

                self->wait.wake_all();
                list = next;
            }
        }

        void client_t::queue_wake()
        {
            if (queued.load(std::memory_order_acquire))
                return;

            {
                const std::unique_lock _ { wake_lock };
                if (queued.exchange(true, std::memory_order_acq_rel))
                    return;

                self_ref = shared_from_this();
                next_pending = wake_head;
                wake_head = this;
            }

            wake_worker->wake();
        }

        lib::initgraph::task wake_worker_task
        {
            "input.evdev.wake-worker",
            lib::initgraph::postsched_init_engine,
            [] {
                wake_worker.initialize("input-evdev", cpu::bsp_idx(), drain);
                lib::bug_on(!wake_worker->start());

                if (const auto ret = register_consumer(consumer); !ret)
                {
                    lib::error(
                        "evdev: could not register consumer: {}",
                        lib::error_name(ret.error())
                    );
                }
            }
        };
    } // namespace
} // namespace input
