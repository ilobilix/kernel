// Copyright (C) 2024-2026  ilobilo

module system.dev;

import drivers.fs.devtmpfs;
import drivers.fs.sysfs;
import system.vfs.dev;
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
            // TODO: guard against recursive probing
            dev->drv = &drv;

            if (const auto res = dev->bus->probe(*dev, drv); !res)
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

        void collect_env(device_t &self, uevent_t &uev)
        {
            if (self.bus != nullptr)
                lib::unused(self.bus->fill_uevent(self, uev));
            if (self.cls != nullptr)
                lib::unused(self.cls->fill_uevent(self, uev));
            if (self.type != nullptr)
                lib::unused(self.type->fill_uevent(self, uev));

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
        lib::debug(
            "dev: uevent {}@{} {}",
            action_name(act), uev.devpath, uev.envp
        );
        return { };
    }

    std::string device_t::uevent_attribute_text()
    {
        uevent_t uev {
            .action = action::add,
            .devpath = path(),
            .envp = { }
        };
        collect_env(*this, uev);

        std::string out;
        for (const auto &line : uev.envp)
        {
            out.append(line);
            out.push_back('\n');
        }
        return out;
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

    void unregister_kobject(std::shared_ptr<kobject_t> kobj)
    {
        if (!kobj)
            return;

        {
            const auto path = kobj->path().str();
            auto locked = kobjects.write_lock();

            if (const auto prnt = kobj->parent.lock())
            {
                auto &kids = prnt->children;
                kids.erase(
                    std::remove_if(kids.begin(), kids.end(),
                        [&](const std::weak_ptr<kobject_t> &wp)
                        {
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
    }

    lib::expect<void> register_bus(bus_t &bus)
    {
        // TODO: types
        auto kobj = std::make_shared<kobject_t>(bus.name, default_ktype(), bus_root());
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
        // TODO: type
        auto kobj = std::make_shared<kobject_t>(cls.name, default_ktype(), class_root());
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

        std::vector<std::shared_ptr<device_t>> devices;
        {
            auto locked = buses.write_lock();
            const auto it = locked->find(drv.bus->name);
            if (it == locked->end())
                return std::unexpected { lib::err::no_such_device };

            // TODO: type
            auto kobj = std::make_shared<kobject_t>(drv.name, default_ktype(), it->second.kobj);
            if (auto res = register_kobject(kobj); !res)
                return res;

            it->second.driver_kobjs[&drv] = std::move(kobj);
            it->second.drivers.push_back(&drv);
            devices = it->second.devices;
        }

        lib::info("dev: registering driver '{}' on bus '{}'", drv.name, drv.bus->name);

        for (const auto &dev : devices)
        {
            if (dev->bound())
                continue;
            if (drv.bus->match(*dev, drv))
                lib::unused(do_probe(dev, drv));
        }
        return { };
    }

    lib::expect<void> register_device(std::shared_ptr<device_t> dev)
    {
        if (!dev)
            return std::unexpected { lib::err::invalid_argument };

        if (auto res = register_kobject(dev); !res)
            return res;

        std::vector<driver_t *> drivers;
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
            drivers = it->second.drivers;
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

        for (auto drv : drivers)
        {
            if (dev->bound())
                break;
            if (dev->bus->match(*dev, *drv))
                lib::unused(do_probe(dev, *drv));
        }

        if (!dev->bound() && !dev->modalias.empty())
        {
            // TODO: module autoload?
            lib::debug(
                "dev: no driver for '{}', modalias '{}'",
                dev->name, dev->modalias
            );
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

    void unregister_device(std::shared_ptr<device_t> dev)
    {
        if (!dev)
            return;

        if (dev->bound())
        {
            lib::unused(dev->bus->remove(*dev, *dev->drv));
            lib::unused(dev->emit(action::unbind));
            unreflect_driver_links(dev, *dev->drv);
            dev->drv = nullptr;
        }

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

        void install_root(
            std::shared_ptr<kobject_t> &slot, std::string_view name,
            std::shared_ptr<kobject_t> parent = { }
        )
        {
            slot = std::make_shared<kobject_t>(name, default_ktype(), parent);
            lib::bug_on(!register_kobject(slot));
        }

        void init()
        {
            install_root(devices_kobj, "devices");
            install_root(bus_kobj, "bus");
            install_root(class_kobj, "class");
            install_root(dev_kobj, "dev");
            install_root(dev_char_kobj, "char",    dev_kobj);
            install_root(dev_block_kobj, "block",   dev_kobj);
            install_root(virtual_kobj, "virtual", devices_kobj);
        }

        lib::initgraph::task init_task
        {
            "dev.init",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::entail { core_registered_stage() },
            [] { init(); }
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
