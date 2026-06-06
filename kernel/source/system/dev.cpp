// Copyright (C) 2024-2026  ilobilo

module system.dev;

import drivers.fs.devtmpfs;
import drivers.fs.sysfs;
import system.vfs.dev;
import system.bin.elf;
import magic_enum;
import fmt;
import lib;
import std;

namespace dev
{
    namespace
    {
        struct bus_state_t
        {
            bus_t *bus = nullptr;
            std::shared_ptr<kobject_t> kobj;
            std::shared_ptr<kobject_t> devices_kobj;
            std::shared_ptr<kobject_t> drivers_kobj;

            std::vector<driver_t *> drivers;
            lib::map::flat_hash<
                driver_t *,
                std::shared_ptr<kobject_t>
            > driver_kobjs;

            std::vector<std::shared_ptr<device_t>> devices;
        };

        struct class_state_t
        {
            class_t *cls = nullptr;
            std::shared_ptr<kobject_t> kobj;
            std::vector<std::shared_ptr<device_t>> devices;
        };

        lib::locker<
            lib::map::flat_hash<
                std::string,
                std::shared_ptr<kobject_t>
            >, lib::rwspinlock
        > kobjects;

        lib::locker<
            lib::map::flat_hash<
                std::string_view,
                bus_state_t
            >, lib::rwspinlock
        > buses;

        lib::locker<
            lib::map::flat_hash<
                std::string_view,
                class_state_t
            >, lib::rwspinlock
        > classes;

        lib::locker<
            std::vector<std::shared_ptr<device_t>>,
            lib::rwspinlock
        > devices;

        std::atomic<reflector_t *> current_ref { nullptr };

        void reflect_device_links(const std::shared_ptr<device_t> &dev)
        {
            const auto ref = current_ref.load(std::memory_order_acquire);
            if (!ref)
                return;

            std::shared_ptr<kobject_t> bus_kobj;
            std::shared_ptr<kobject_t> bus_devices_kobj;
            std::shared_ptr<kobject_t> class_kobj;

            if (dev->bus != nullptr)
            {
                const auto locked = buses.read_lock();
                if (const auto it = locked->find(dev->bus->name); it != locked->end())
                {
                    bus_kobj = it->second.kobj;
                    bus_devices_kobj = it->second.devices_kobj;
                }
            }

            if (dev->cls != nullptr)
            {
                const auto locked = classes.read_lock();
                if (const auto it = locked->find(dev->cls->name); it != locked->end())
                    class_kobj = it->second.kobj;
            }

            const auto devpath = dev->path();

            if (bus_kobj)
                ref->add_link(dev, "subsystem", bus_kobj->path());
            else if (class_kobj)
                ref->add_link(dev, "subsystem", class_kobj->path());

            if (bus_devices_kobj)
                ref->add_link(bus_devices_kobj, dev->name, devpath);

            if (class_kobj)
                ref->add_link(class_kobj, dev->name, devpath);

            if (dev->devt != 0)
            {
                const auto node = (dev->cls != nullptr && dev->cls->is_block)
                    ? dev_block_root() : dev_char_root();
                const auto maj = vfs::dev::major(dev->devt);
                const auto min = vfs::dev::minor(dev->devt);
                ref->add_link(node, fmt::format("{}:{}", maj, min), devpath);
            }
        }

        void unreflect_device_links(const std::shared_ptr<device_t> &dev)
        {
            const auto ref = current_ref.load(std::memory_order_acquire);
            if (!ref)
                return;

            std::shared_ptr<kobject_t> bus_devices_kobj;
            std::shared_ptr<kobject_t> class_kobj;

            if (dev->bus != nullptr)
            {
                const auto locked = buses.read_lock();
                if (const auto it = locked->find(dev->bus->name); it != locked->end())
                    bus_devices_kobj = it->second.devices_kobj;
            }

            if (dev->cls != nullptr)
            {
                const auto locked = classes.read_lock();
                if (const auto it = locked->find(dev->cls->name); it != locked->end())
                    class_kobj = it->second.kobj;
            }

            if (dev->devt != 0)
            {
                const auto node = (dev->cls != nullptr && dev->cls->is_block)
                    ? dev_block_root() : dev_char_root();
                const auto maj = vfs::dev::major(dev->devt);
                const auto min = vfs::dev::minor(dev->devt);
                ref->remove_link(node, fmt::format("{}:{}", maj, min));
            }

            if (class_kobj)
                ref->remove_link(class_kobj, dev->name);
            if (bus_devices_kobj)
                ref->remove_link(bus_devices_kobj, dev->name);

            ref->remove_link(dev, "subsystem");
        }

        std::shared_ptr<kobject_t> driver_kobj_of(driver_t &drv)
        {
            if (drv.bus == nullptr)
                return nullptr;

            const auto locked = buses.read_lock();
            if (const auto it = locked->find(drv.bus->name); it != locked->end())
            {
                const auto &kobjs = it->second.driver_kobjs;
                if (const auto dit = kobjs.find(&drv); dit != kobjs.end())
                    return dit->second;
            }
            return nullptr;
        }

        void reflect_driver_links(const std::shared_ptr<device_t> &dev, driver_t &drv)
        {
            const auto ref = current_ref.load(std::memory_order_acquire);
            if (!ref)
                return;

            const auto driver_kobj = driver_kobj_of(drv);
            if (!driver_kobj)
                return;

            ref->add_link(dev, "driver", driver_kobj->path());
            ref->add_link(driver_kobj, dev->name, dev->path());
        }

        void unreflect_driver_links(const std::shared_ptr<device_t> &dev, driver_t &drv)
        {
            const auto ref = current_ref.load(std::memory_order_acquire);
            if (!ref)
                return;

            if (const auto driver_kobj = driver_kobj_of(drv))
                ref->remove_link(driver_kobj, dev->name);
            ref->remove_link(dev, "driver");
        }

        lib::expect<void> do_probe(const std::shared_ptr<device_t> &dev, driver_t &drv)
        {
            if (dev->probing)
            {
                lib::warn("dev: recursive probe of '{}' detected", dev->name);
                return std::unexpected { lib::err::already_in_progress };
            }

            dev->probing = true;
            dev->drv = &drv;

            const auto res = dev->bus->probe(*dev, drv);
            dev->probing = false;

            if (!res)
            {
                dev->drv = nullptr;
                lib::warn(
                    "dev: probing '{}' with driver '{}' failed: {}",
                    dev->name, drv.name, lib::error_name(res.error())
                );
                return res;
            }

            lib::info("dev: bound '{}' to driver '{}'", dev->name, drv.name);
            lib::unused(dev->emit(action::bind));
            reflect_driver_links(dev, drv);
            return { };
        }

        void probe_device(const std::shared_ptr<device_t> &dev)
        {
            if (dev->bus == nullptr || dev->bound())
                return;

            std::vector<driver_t *> drivers;
            {
                const auto locked = buses.read_lock();
                if (const auto it = locked->find(dev->bus->name); it != locked->end())
                    drivers = it->second.drivers;
            }

            for (auto drv : drivers)
            {
                if (dev->bound())
                    break;
                if (dev->bus->match(*dev, *drv))
                    lib::unused(do_probe(dev, *drv));
            }
        }

        lib::expect<void> probe_bus(bus_t &bus, std::string_view name)
        {
            for (const auto &dev : bus.get_devices())
            {
                if (dev->name != name)
                    continue;
                if (dev->bound())
                    return std::unexpected { lib::err::target_is_busy };
                probe_device(dev);
                return { };
            }
            return std::unexpected { lib::err::no_such_device };
        }

        void do_unbind(const std::shared_ptr<device_t> &dev)
        {
            auto &drv = *dev->drv;
            if (!dev->bus->remove(*dev, drv))
                lib::warn("dev: driver '{}' failed to remove '{}'", drv.name, dev->name);

            lib::info("dev: unbound '{}' from driver '{}'", dev->name, drv.name);
            lib::unused(dev->emit(action::unbind));
            unreflect_driver_links(dev, drv);
            dev->drv = nullptr;
        }

        void collect_env(device_t &self, uevent_t &uev)
        {
            if (self.bus != nullptr)
                self.bus->fill_uevent(self, uev);
            if (self.cls != nullptr)
                self.cls->fill_uevent(self, uev);
            if (self.type != nullptr)
                self.type->fill_uevent(self, uev);

            if (!self.modalias.empty())
                uev.add("MODALIAS", self.modalias);

            if (self.devt != 0)
            {
                uev.add("MAJOR", std::to_string(vfs::dev::major(self.devt)));
                uev.add("MINOR", std::to_string(vfs::dev::minor(self.devt)));

                if (self.cls != nullptr)
                {
                    mode_t mode { };
                    if (auto devname = self.cls->devnode(self, mode); !devname.empty())
                        uev.add("DEVNAME", devname);
                }
            }

            for (const auto &[key, value] : self.props)
                uev.add(key, value);
        }

        std::string format_uevent(const uevent_t &uev)
        {
            std::string out;
            for (const auto &line : uev.envp)
            {
                out.append(line);
                out.push_back('\n');
            }
            return out;
        }

        struct bind_attribute_t : attribute_t
        {
            bind_attribute_t() : attribute_t { "bind", 0200 } { }

            lib::expect<void> store(kobject_t &kobj, std::string_view data) override
            {
                return bind_device(static_cast<driver_kobject_t &>(kobj).drv, lib::trim(data));
            }
        };

        struct unbind_attribute_t : attribute_t
        {
            unbind_attribute_t() : attribute_t { "unbind", 0200 } { }

            lib::expect<void> store(kobject_t &kobj, std::string_view data) override
            {
                return unbind_device(static_cast<driver_kobject_t &>(kobj).drv, lib::trim(data));
            }
        };

        struct driver_ktype_t : ktype_t
        {
            std::span<attribute_t *const> attributes() override
            {
                static attribute_t *attrs[] { bind_attribute(), unbind_attribute() };
                return attrs;
            }
        };

        bus_t &bus_of(kobject_t &kobj)
        {
            return static_cast<bus_kobject_t &>(kobj).bus;
        }

        struct autoprobe_attribute_t : attribute_t
        {
            autoprobe_attribute_t() : attribute_t { "drivers_autoprobe", 0644 } { }

            lib::expect<std::string> show(kobject_t &kobj) override
            {
                return bus_of(kobj).drivers_autoprobe ? "1\n" : "0\n";
            }

            lib::expect<void> store(kobject_t &kobj, std::string_view data) override
            {
                const auto value = lib::trim(data);
                if (value == "0")
                    bus_of(kobj).drivers_autoprobe = false;
                else if (value == "1")
                    bus_of(kobj).drivers_autoprobe = true;
                else
                    return std::unexpected { lib::err::invalid_argument };
                return { };
            }
        };

        struct drivers_probe_attribute_t : attribute_t
        {
            drivers_probe_attribute_t() : attribute_t { "drivers_probe", 0200 } { }

            lib::expect<void> store(kobject_t &kobj, std::string_view data) override
            {
                return probe_bus(bus_of(kobj), lib::trim(data));
            }
        };

        struct bus_ktype_t : ktype_t
        {
            std::span<attribute_t *const> attributes() override
            {
                static autoprobe_attribute_t autoprobe { };
                static drivers_probe_attribute_t drivers_probe { };
                static attribute_t *attrs[] { &autoprobe, &drivers_probe };
                return attrs;
            }
        };
    } // namespace

    void uevent_t::add(std::string_view key, std::string_view value)
    {
        envp.push_back(fmt::format("{}={}", key, value));
    }

    std::vector<std::shared_ptr<device_t>> bus_t::get_devices()
    {
        const auto locked = buses.read_lock();
        if (const auto it = locked->find(name); it != locked->end())
            return it->second.devices;
        return { };
    }

    std::vector<std::shared_ptr<device_t>> class_t::get_devices()
    {
        const auto locked = classes.read_lock();
        if (const auto it = locked->find(name); it != locked->end())
            return it->second.devices;
        return { };
    }

    lib::expect<void> device_t::emit(dev::action act)
    {
        uevent_t uev {
            .action = act,
            .devpath = path(),
            .envp = { }
        };

        const auto subsystem =
            bus ? bus->name :
            cls ? cls->name :
            std::string_view { };

        uev.add("ACTION", action_name(act));
        uev.add("DEVPATH", uev.devpath);
        if (!subsystem.empty())
            uev.add("SUBSYSTEM", subsystem);

        collect_env(*this, uev);

        // TODO: NETLINK_KOBJECT_UEVENT
        // lib::debug("dev: uevent {}", uev.envp);
        return { };
    }

    lib::expect<void> kobject_t::emit(action act)
    {
        uevent_t uev {
            .action = act,
            .devpath = path(),
            .envp = { }
        };

        uev.add("ACTION", action_name(act));
        uev.add("DEVPATH", uev.devpath);
        if (type != nullptr)
            type->fill_uevent(*this, uev);

        // TODO: NETLINK_KOBJECT_UEVENT
        return { };
    }

    lib::expect<void> uevent_store(kobject_t &kobj, std::string_view data)
    {
        const auto act = magic_enum::enum_cast<action>(lib::trim(data));
        if (!act)
            return std::unexpected { lib::err::invalid_argument };
        return kobj.emit(*act);
    }

    std::string kobject_t::uevent_text()
    {
        uevent_t uev {
            .action = action::add,
            .devpath = path(),
            .envp = { }
        };
        if (type != nullptr)
            type->fill_uevent(*this, uev);
        return format_uevent(uev);
    }

    std::string device_t::uevent_text()
    {
        uevent_t uev {
            .action = action::add,
            .devpath = path(),
            .envp = { }
        };
        collect_env(*this, uev);
        return format_uevent(uev);
    }

    void attach_reflector(reflector_t *ref)
    {
        current_ref.store(ref, std::memory_order_release);

        const auto kobjs = *kobjects.read_lock();
        const auto devs = *devices.read_lock();

        for (const auto &[name, obj] : kobjs)
            ref->add_object(obj);

        for (const auto &dev : devs)
        {
            ref->add_object(dev);
            reflect_device_links(dev);
            if (dev->bound())
                reflect_driver_links(dev, *dev->drv);
        }
    }

    void detach_reflector()
    {
        current_ref.store(nullptr, std::memory_order_release);
    }

    lib::expect<void> register_kobject(std::shared_ptr<kobject_t> kobj)
    {
        if (!kobj)
            return std::unexpected { lib::err::invalid_argument };

        {
            const auto path = kobj->path().str();
            auto locked = kobjects.write_lock();
            if (locked->contains(path))
                return std::unexpected { lib::err::already_exists };

            if (const auto prnt = kobj->parent.lock())
                prnt->children.push_back(kobj);

            lib::debug("dev: registering kobject '{}'", path);
            locked.value()[path] = kobj;
        }

        if (auto ref = current_ref.load(std::memory_order_acquire))
            ref->add_object(std::move(kobj));
        return { };
    }

    bool unregister_kobject(std::shared_ptr<kobject_t> kobj)
    {
        if (!kobj)
            return false;

        {
            const auto path = kobj->path().str();
            auto locked = kobjects.write_lock();

            if (const auto prnt = kobj->parent.lock())
            {
                auto &kids = prnt->children;
                kids.erase(
                    std::remove_if(kids.begin(), kids.end(),
                        [&](const std::weak_ptr<kobject_t> &wp) {
                            const auto sp = wp.lock();
                            return !sp || sp.get() == kobj.get();
                        }
                    ), kids.end()
                );
            }

            if (const auto it = locked->find(path); it != locked->end())
            {
                lib::debug("dev: unregistering kobject '{}'", path);
                locked->erase(it);
            }
        }

        if (auto ref = current_ref.load(std::memory_order_acquire))
            ref->remove_object(kobj);
        return true;
    }

    lib::expect<void> register_bus(bus_t &bus)
    {
        auto kobj = std::make_shared<bus_kobject_t>(
            bus.name, bus.type ?: bus_ktype(), bus_root(), bus
        );
        if (auto res = register_kobject(kobj); !res)
            return res;

        auto devices_kobj = std::make_shared<kobject_t>("devices", default_ktype(), kobj);
        if (auto res = register_kobject(devices_kobj); !res)
        {
            unregister_kobject(kobj);
            return res;
        }

        auto drivers_kobj = std::make_shared<kobject_t>("drivers", default_ktype(), kobj);
        if (auto res = register_kobject(drivers_kobj); !res)
        {
            unregister_kobject(kobj);
            unregister_kobject(devices_kobj);
            return res;
        }

        auto locked = buses.write_lock();
        if (locked->contains(bus.name))
            return std::unexpected { lib::err::already_exists };

        lib::info("dev: registering bus '{}'", bus.name);
        locked.value()[bus.name] = bus_state_t {
            .bus = &bus,
            .kobj = std::move(kobj),
            .devices_kobj = std::move(devices_kobj),
            .drivers_kobj = std::move(drivers_kobj),
            .drivers = { },
            .driver_kobjs = { },
            .devices = { }
        };
        return { };
    }

    lib::expect<void> register_class(class_t &cls)
    {
        auto kobj = std::make_shared<kobject_t>(
            cls.name, cls.type ?: default_ktype(), class_root()
        );
        if (auto res = register_kobject(kobj); !res)
            return res;

        auto locked = classes.write_lock();
        if (locked->contains(cls.name))
            return std::unexpected { lib::err::already_exists };

        lib::info("dev: registering class '{}'", cls.name);
        locked.value()[cls.name] = class_state_t {
            .cls = &cls,
            .kobj = std::move(kobj),
            .devices = { }
        };
        return { };
    }

    lib::expect<void> register_driver(driver_t &drv)
    {
        if (drv.bus == nullptr)
            return std::unexpected { lib::err::invalid_argument };

        {
            auto locked = buses.write_lock();
            const auto it = locked->find(drv.bus->name);
            if (it == locked->end())
                return std::unexpected { lib::err::no_such_device };

            auto kobj = std::make_shared<driver_kobject_t>(
                drv.name, drv.type ?: default_ktype(), it->second.drivers_kobj, drv
            );
            if (auto res = register_kobject(kobj); !res)
                return res;

            it->second.driver_kobjs[&drv] = std::move(kobj);
            it->second.drivers.push_back(&drv);
        }

        lib::info("dev: registering driver '{}' on bus '{}'", drv.name, drv.bus->name);

        probe_driver(drv);
        return { };
    }

    void probe_driver(driver_t &drv)
    {
        if (drv.bus == nullptr)
            return;

        for (const auto &dev : drv.bus->get_devices())
        {
            if (dev->bound())
                continue;
            if (drv.bus->match(*dev, drv))
                lib::unused(do_probe(dev, drv));
        }
    }

    lib::expect<void> bind_device(driver_t &drv, std::string_view name)
    {
        if (drv.bus == nullptr)
            return std::unexpected { lib::err::invalid_argument };

        for (const auto &dev : drv.bus->get_devices())
        {
            if (dev->name != name)
                continue;
            if (dev->bound())
                return std::unexpected { lib::err::target_is_busy };
            return do_probe(dev, drv);
        }
        return std::unexpected { lib::err::no_such_device };
    }

    lib::expect<void> unbind_device(driver_t &drv, std::string_view name)
    {
        if (drv.bus == nullptr)
            return std::unexpected { lib::err::invalid_argument };

        for (const auto &dev : drv.bus->get_devices())
        {
            if (dev->name != name)
                continue;
            if (dev->drv != &drv)
                return std::unexpected { lib::err::invalid_argument };
            do_unbind(dev);
            return { };
        }
        return std::unexpected { lib::err::no_such_device };
    }

    attribute_t *bind_attribute()
    {
        static bind_attribute_t attr { };
        return &attr;
    }

    attribute_t *unbind_attribute()
    {
        static unbind_attribute_t attr { };
        return &attr;
    }

    ktype_t *driver_ktype()
    {
        static driver_ktype_t ktype { };
        return &ktype;
    }

    ktype_t *bus_ktype()
    {
        static bus_ktype_t ktype { };
        return &ktype;
    }

    bool unregister_driver(driver_t &drv)
    {
        if (drv.bus == nullptr)
            return false;

        std::vector<std::shared_ptr<device_t>> bound;
        std::shared_ptr<kobject_t> drv_kobj;
        {
            auto locked = buses.write_lock();
            const auto it = locked->find(drv.bus->name);
            if (it == locked->end())
                return false;

            auto &st = it->second;
            for (const auto &dev : st.devices)
            {
                if (dev->drv == &drv)
                    bound.push_back(dev);
            }

            auto &drvs = st.drivers;
            drvs.erase(std::remove(drvs.begin(), drvs.end(), &drv), drvs.end());

            if (auto kobj = st.driver_kobjs.find(&drv); kobj != st.driver_kobjs.end())
            {
                drv_kobj = std::move(kobj->second);
                st.driver_kobjs.erase(kobj);
            }
        }

        for (auto &dev : bound)
            do_unbind(dev);

        if (drv_kobj)
            unregister_kobject(drv_kobj);

        // TODO: module unload?

        return true;
    }

    lib::expect<void> register_device(std::shared_ptr<device_t> dev)
    {
        if (!dev)
            return std::unexpected { lib::err::invalid_argument };

        if (auto res = register_kobject(dev); !res)
            return res;

        if (dev->bus != nullptr)
        {
            auto locked = buses.write_lock();
            const auto it = locked->find(dev->bus->name);
            if (it == locked->end())
            {
                unregister_kobject(dev);
                return std::unexpected { lib::err::no_such_device };
            }

            it->second.devices.push_back(dev);
        }

        if (dev->cls != nullptr)
        {
            auto locked = classes.write_lock();
            if (const auto it = locked->find(dev->cls->name); it != locked->end())
                it->second.devices.push_back(dev);
        }

        devices.write_lock()->push_back(dev);

        lib::info(
            "dev: registering device '{}'{}",
            dev->name, dev->bus ? fmt::format(" on bus '{}'", dev->bus->name) : ""
        );

        lib::unused(dev->emit(action::add));
        reflect_device_links(dev);

        if (dev->bus == nullptr || dev->bus->drivers_autoprobe)
        {
            probe_device(dev);

            if (!dev->bound() && !dev->modalias.empty() &&
                !bin::elf::mod::request_alias(dev->modalias))
            {
                lib::debug(
                    "dev: no driver for '{}', modalias '{}'",
                    dev->name, dev->modalias
                );
            }
        }

        if (dev->devt != 0 && dev->fops)
        {
            vfs::dev::register_ops(dev->devt, dev->fops);

            mode_t mode = 0660;
            if (dev->cls && dev->cls->is_block)
                mode |= stat::s_ifblk;
            else
                mode |= stat::s_ifchr;

            auto name = dev->name;
            if (dev->cls)
            {
                auto node = dev->cls->devnode(*dev, mode);
                if (!node.empty())
                    name = std::move(node);
            }

            if (const auto ret = fs::devtmpfs::create(name, mode, dev->devt); !ret)
                lib::error("dev: failed to create device node for '{}'", dev->name);
        }

        return { };
    }

    bool unregister_device(std::shared_ptr<device_t> dev)
    {
        if (!dev)
            return false;

        if (dev->bound())
            do_unbind(dev);

        if (dev->bus != nullptr)
        {
            auto locked = buses.write_lock();
            if (const auto it = locked->find(dev->bus->name); it != locked->end())
            {
                auto &devs = it->second.devices;
                devs.erase(std::remove(devs.begin(), devs.end(), dev), devs.end());
            }
        }

        if (dev->cls != nullptr)
        {
            auto locked = classes.write_lock();
            if (const auto it = locked->find(dev->cls->name); it != locked->end())
            {
                auto &devs = it->second.devices;
                devs.erase(std::remove(devs.begin(), devs.end(), dev), devs.end());
            }
        }

        {
            auto locked = devices.write_lock();
            locked->erase(std::remove(locked->begin(), locked->end(), dev), locked->end());
        }

        if (dev->devt != 0 && dev->fops)
        {
            vfs::dev::unregister_ops(dev->devt);

            auto name = dev->name;
            if (dev->cls)
            {
                mode_t mode = 0;
                auto node = dev->cls->devnode(*dev, mode);
                if (!node.empty())
                    name = std::move(node);
            }

            if (const auto ret = fs::devtmpfs::remove(name); !ret)
                lib::error("dev: failed to remove device node for '{}'", dev->name);
        }

        unreflect_device_links(dev);
        lib::unused(dev->emit(action::remove));
        unregister_kobject(dev);

        return true;
    }

    lib::initgraph::stage *core_registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "dev.core-registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *available_stage()
    {
        static lib::initgraph::stage stage
        {
            "dev.available",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    ktype_t *default_ktype()
    {
        static ktype_t instance { };
        return &instance;
    }

    namespace
    {
        std::shared_ptr<kobject_t> devices_kobj;
        std::shared_ptr<kobject_t> bus_kobj;
        std::shared_ptr<kobject_t> class_kobj;
        std::shared_ptr<kobject_t> dev_kobj;
        std::shared_ptr<kobject_t> dev_char_kobj;
        std::shared_ptr<kobject_t> dev_block_kobj;
        std::shared_ptr<kobject_t> virtual_kobj;

        void init()
        {
            const auto install_root = [&](
                std::shared_ptr<kobject_t> &slot, std::string_view name,
                std::shared_ptr<kobject_t> parent = { }
            ) {
                slot = std::make_shared<kobject_t>(name, default_ktype(), parent);
                lib::bug_on(!register_kobject(slot));
            };

            install_root(devices_kobj, "devices", nullptr);
            install_root(bus_kobj, "bus", nullptr);
            install_root(class_kobj, "class", nullptr);
            install_root(dev_kobj, "dev", nullptr);
            install_root(dev_char_kobj, "char", dev_kobj);
            install_root(dev_block_kobj, "block", dev_kobj);
            install_root(virtual_kobj, "virtual", devices_kobj);
        }

        lib::initgraph::task init_task
        {
            "dev.init",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::entail { core_registered_stage() },
            [] {
                init();
            }
        };

        lib::initgraph::task available_task
        {
            "dev.available",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require {
                core_registered_stage(),
                fs::sysfs::registered_stage(),
                fs::devtmpfs::registered_stage()
            },
            lib::initgraph::entail { available_stage() },
            [] { }
        };
    } // namespace

    std::shared_ptr<kobject_t> devices_root()
    {
        lib::bug_on(!devices_kobj);
        return devices_kobj;
    }

    std::shared_ptr<kobject_t> bus_root()
    {
        lib::bug_on(!bus_kobj);
        return bus_kobj;
    }

    std::shared_ptr<kobject_t> class_root()
    {
        lib::bug_on(!class_kobj);
        return class_kobj;
    }

    std::shared_ptr<kobject_t> dev_char_root()
    {
        lib::bug_on(!dev_char_kobj);
        return dev_char_kobj;
    }

    std::shared_ptr<kobject_t> dev_block_root()
    {
        lib::bug_on(!dev_block_kobj);
        return dev_block_kobj;
    }

    std::shared_ptr<kobject_t> virtual_root()
    {
        lib::bug_on(!virtual_kobj);
        return virtual_kobj;
    }
} // namespace dev
