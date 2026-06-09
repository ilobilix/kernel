// Copyright (C) 2024-2026  ilobilo

export module system.dev;

import system.memory.virt;
import system.vfs;
import magic_enum;
import lib;
import std;

export namespace dev
{
    enum class action
    {
        add,
        remove,
        change,
        bind,
        unbind
    };

    constexpr auto action_name(action act)
    {
        lib::bug_on(!magic_enum::enum_contains(act));
        return magic_enum::enum_name(act);
    }

    struct uevent_t
    {
        action action;
        std::string devpath;
        std::vector<std::string> envp;

        void add(std::string_view key, std::string_view value);
    };

    struct kobject_t;
    struct attribute_t
    {
        std::string name;
        mode_t mode;

        attribute_t(std::string_view name, mode_t mode)
            : name { name }, mode { mode } { }

        virtual lib::expect<std::string> show(kobject_t &kobj)
        {
            lib::unused(kobj);
            return std::unexpected { lib::err::io_error };
        }

        virtual lib::expect<void> store(kobject_t &kobj, std::string_view data)
        {
            lib::unused(kobj, data);
            return std::unexpected { lib::err::io_error };
        }

        virtual ~attribute_t() = default;
    };

    struct bin_attribute_t
    {
        std::string name;
        mode_t mode;

        bin_attribute_t(std::string_view name, mode_t mode)
            : name { name }, mode { mode } { }

        virtual std::size_t size(kobject_t &kobj)
        {
            lib::unused(kobj);
            return 0;
        }

        virtual lib::expect<std::size_t> read(
            kobject_t &kobj, std::span<std::byte> buffer, std::size_t offset
        )
        {
            lib::unused(kobj, buffer, offset);
            return std::unexpected { lib::err::io_error };
        }

        virtual lib::expect<std::size_t> write(
            kobject_t &kobj, std::span<const std::byte> buffer, std::size_t offset
        )
        {
            lib::unused(kobj, buffer, offset);
            return std::unexpected { lib::err::io_error };
        }

        virtual lib::expect<vmm::object::ptr> mmap(kobject_t &kobj)
        {
            lib::unused(kobj);
            return std::unexpected { lib::err::mapping_unsupported };
        }

        virtual ~bin_attribute_t() = default;
    };

    struct ktype_t
    {
        virtual std::span<attribute_t *const> attributes()
        {
            return { };
        }

        virtual std::span<bin_attribute_t *const> bin_attributes()
        {
            return { };
        }

        virtual void fill_uevent(kobject_t &kobj, uevent_t &uev)
        {
            lib::unused(kobj, uev);
        }

        virtual ~ktype_t() = default;
    };

    struct device_t;
    struct kobject_t
    {
        std::string name;
        std::weak_ptr<kobject_t> parent;
        ktype_t *type;

        std::vector<std::weak_ptr<kobject_t>> children;

        kobject_t(std::string_view name, ktype_t *type, std::weak_ptr<kobject_t> parent = { })
            : name { name }, parent { parent }, type { type } { }

        kobject_t(const kobject_t &) = delete;
        kobject_t &operator=(const kobject_t &) = delete;

        lib::path path() const
        {
            if (const auto prnt = parent.lock())
                return prnt->path() / name;
            return lib::path { "/" } / name;
        }

        virtual device_t *as_device() { return nullptr; }
        virtual std::string uevent_text();
        virtual lib::expect<void> emit(action act);

        virtual ~kobject_t() = default;
    };

    struct bus_t;
    struct driver_t
    {
        std::string name;
        bus_t *bus;
        ktype_t *type = nullptr;

        driver_t(std::string_view name, bus_t *bus)
            : name { name }, bus { bus } { }

        virtual lib::expect<void> probe(device_t &dev) = 0;
        virtual bool remove(device_t &dev)
        {
            lib::unused(dev);
            return true;
        }

        virtual ~driver_t() = default;
    };

    struct driver_kobject_t : kobject_t
    {
        driver_t &drv;

        driver_kobject_t(
            std::string_view name, ktype_t *type,
            std::weak_ptr<kobject_t> parent, driver_t &drv
        ) : kobject_t { name, type, parent }, drv { drv } { }
    };

    struct bus_t
    {
        std::string name;
        ktype_t *type = nullptr;
        bool drivers_autoprobe = true;

        bus_t(std::string_view name) : name { name } { }

        std::vector<std::shared_ptr<device_t>> get_devices();

        virtual bool match(device_t &dev, driver_t &drv) = 0;

        virtual lib::expect<void> probe(device_t &dev, driver_t &drv)
        {
            return drv.probe(dev);
        }

        virtual bool remove(device_t &dev, driver_t &drv)
        {
            return drv.remove(dev);
        }

        virtual void fill_uevent(device_t &dev, uevent_t &uev)
        {
            lib::unused(dev, uev);
        }

        virtual ~bus_t() = default;
    };

    struct bus_kobject_t : kobject_t
    {
        bus_t &bus;

        bus_kobject_t(
            std::string_view name, ktype_t *type,
            std::weak_ptr<kobject_t> parent, bus_t &bus
        ) : kobject_t { name, type, parent }, bus { bus } { }
    };

    struct class_t
    {
        std::string name;
        bool is_block;
        ktype_t *type = nullptr;

        class_t(std::string_view name, bool is_block = false)
            : name { name }, is_block { is_block } { }

        std::vector<std::shared_ptr<device_t>> get_devices();

        virtual std::string devnode(device_t &dev, mode_t &mode)
        {
            lib::unused(dev, mode);
            return { };
        }

        virtual void fill_uevent(device_t &dev, uevent_t &uev)
        {
            lib::unused(dev, uev);
        }

        virtual ~class_t() = default;
    };

    struct device_t : kobject_t
    {
        static inline std::atomic_size_t next_id = 0;
        const std::size_t id;

        bus_t *bus = nullptr;
        driver_t *drv = nullptr;
        class_t *cls = nullptr;

        dev_t devt = 0;
        std::shared_ptr<vfs::ops> fops;
        std::string modalias;

        bool probing = false;

        std::vector<std::pair<std::string, std::string>> props;

        device_t(std::string_view name, ktype_t *type)
            : kobject_t { name, type }, id { next_id.fetch_add(1, std::memory_order_relaxed) } { }

        device_t *as_device() override { return this; }

        bool bound() const { return drv != nullptr; }

        lib::expect<void> emit(action act) override;
        std::string uevent_text() override;
    };

    struct reflector_t
    {
        virtual void add_object(const std::shared_ptr<kobject_t> &kobj) = 0;
        virtual void remove_object(const std::shared_ptr<kobject_t> &kobj) = 0;
        virtual void add_link(
            const std::shared_ptr<kobject_t> &dir,
            std::string_view name, const lib::path &target
        ) = 0;
        virtual void remove_link(const std::shared_ptr<kobject_t> &dir, std::string_view name) = 0;

        virtual ~reflector_t() = default;
    };

    void attach_reflector(reflector_t *ref);
    void detach_reflector();

    lib::expect<void> register_kobject(std::shared_ptr<kobject_t> kobj);
    bool unregister_kobject(std::shared_ptr<kobject_t> kobj);

    lib::expect<void> register_bus(bus_t &bus);
    lib::expect<void> register_class(class_t &cls);

    lib::expect<void> register_driver(driver_t &drv);
    bool unregister_driver(driver_t &drv);

    lib::expect<void> bind_device(driver_t &drv, std::string_view name);
    lib::expect<void> unbind_device(driver_t &drv, std::string_view name);

    void probe_driver(driver_t &drv);

    lib::expect<void> uevent_store(kobject_t &kobj, std::string_view data);

    attribute_t *bind_attribute();
    attribute_t *unbind_attribute();

    ktype_t *driver_ktype();
    ktype_t *bus_ktype();

    lib::expect<void> register_device(std::shared_ptr<device_t> dev);
    bool unregister_device(std::shared_ptr<device_t> dev);

    lib::initgraph::stage *core_registered_stage();
    lib::initgraph::stage *available_stage();

    ktype_t *default_ktype();

    std::shared_ptr<kobject_t> devices_root();
    std::shared_ptr<kobject_t> bus_root();
    std::shared_ptr<kobject_t> class_root();
    std::shared_ptr<kobject_t> dev_char_root();
    std::shared_ptr<kobject_t> dev_block_root();
    std::shared_ptr<kobject_t> virtual_root();
} // export namespace dev
