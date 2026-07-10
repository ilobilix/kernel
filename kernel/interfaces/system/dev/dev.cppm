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

    class kobject_t;
    class attribute_t
    {
        public:
        const std::string name;
        const mode_t mode;

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

    class bin_attribute_t
    {
        public:
        const std::string name;
        const mode_t mode;

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

        virtual lib::expect<vmm::object::ptr> map(kobject_t &kobj)
        {
            lib::unused(kobj);
            return std::unexpected { lib::err::mapping_unsupported };
        }

        virtual ~bin_attribute_t() = default;
    };

    class ktype_t
    {
        private:
        static inline std::size_t next_id = 0;

        public:
        const std::size_t id = next_id++;

        bool operator==(const ktype_t &rhs) const
        {
            return id == rhs.id;
        }

        virtual std::span<attribute_t> attributes() const
        {
            return { };
        }

        virtual std::span<bin_attribute_t> bin_attributes() const
        {
            return { };
        }

        virtual void fill_uevent(kobject_t &kobj, uevent_t &uev)
        {
            lib::unused(kobj, uev);
        }

        virtual ~ktype_t() = default;
    };

    class device_t;
    class kobject_t : public std::enable_shared_from_this<kobject_t>
    {
        protected:
        kobject_t(std::string_view name, ktype_t &type, std::weak_ptr<kobject_t> parent)
            : name { name }, parent { parent }, type { type } { }

        public:
        const std::string name;
        const std::weak_ptr<kobject_t> parent;
        ktype_t &type;
        std::vector<std::weak_ptr<kobject_t>> children;

        template<typename ...Args>
        kobject_t(lib::private_t<kobject_t>, Args &&...args)
            : kobject_t { std::forward<Args>(args)... } { };

        kobject_t(const kobject_t &) = delete;
        kobject_t &operator=(const kobject_t &) = delete;

        static std::shared_ptr<kobject_t> create(
            std::string_view name, ktype_t &type, std::weak_ptr<kobject_t> parent
        )
        {
            return std::make_shared<kobject_t>(lib::private_t<kobject_t> { }, name, type, parent);
        }

        std::weak_ptr<kobject_t> as_weak() { return weak_from_this(); }
        std::shared_ptr<kobject_t> as_shared() { return shared_from_this(); }

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

    class bus_t;
    class driver_t
    {
        public:
        const std::string name;
        bus_t *const bus;
        ktype_t &type;

        driver_t(std::string_view name, bus_t *const bus, ktype_t &type)
            : name { name }, bus { bus }, type { type } { }

        driver_t(const driver_t &) = delete;
        driver_t &operator=(const driver_t &) = delete;

        virtual lib::expect<void> probe(device_t &dev) = 0;
        virtual bool remove(device_t &dev)
        {
            lib::unused(dev);
            return true;
        }

        virtual ~driver_t() = default;
    };

    class driver_kobject_t final : public kobject_t
    {
        public:
        driver_t &drv;

        driver_kobject_t(
            lib::private_t<driver_kobject_t>,
            std::string_view name, ktype_t &type,
            std::weak_ptr<kobject_t> parent, driver_t &drv
        ) : kobject_t { name, type, parent }, drv { drv } { }

        driver_kobject_t(const driver_kobject_t &) = delete;
        driver_kobject_t &operator=(const driver_kobject_t &) = delete;

        static std::shared_ptr<driver_kobject_t> create(
            std::string_view name, ktype_t &type,
            std::weak_ptr<kobject_t> parent, driver_t &drv
        )
        {
            return std::make_shared<driver_kobject_t>(
                lib::private_t<driver_kobject_t> { },
                name, type, parent, drv
            );
        }
    };

    class bus_t
    {
        public:
        const std::string name;
        ktype_t &type;
        bool drivers_autoprobe = true;

        bus_t(std::string_view name, ktype_t &type)
            : name { name }, type { type } { }

        bus_t(const bus_t &) = delete;
        bus_t &operator=(const bus_t &) = delete;

        std::vector<std::shared_ptr<device_t>> get_devices();

        virtual bool match(device_t &dev, driver_t &drv) const = 0;

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

    struct bus_kobject_t final : public kobject_t
    {
        bus_t &bus;

        bus_kobject_t(
            lib::private_t<bus_kobject_t>,
            std::string_view name, ktype_t &type,
            std::weak_ptr<kobject_t> parent, bus_t &bus
        ) : kobject_t { name, type, parent }, bus { bus } { }

        bus_kobject_t(const bus_kobject_t &) = delete;
        bus_kobject_t &operator=(const bus_kobject_t &) = delete;

        static std::shared_ptr<bus_kobject_t> create(
            std::string_view name, ktype_t &type,
            std::weak_ptr<kobject_t> parent, bus_t &bus
        )
        {
            return std::make_shared<bus_kobject_t>(
                lib::private_t<bus_kobject_t> { },
                name, type, parent, bus
            );
        }
    };

    class class_t
    {
        public:
        const std::string name;
        ktype_t &type;
        const bool is_block;

        class_t(std::string_view name, ktype_t &type, bool is_block)
            : name { name }, type { type }, is_block { is_block } { }

        class_t(const class_t &) = delete;
        class_t &operator=(const class_t &) = delete;

        std::vector<std::shared_ptr<device_t>> get_devices();

        virtual std::string devnode(const device_t &dev, mode_t &mode) const
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

    class device_t : public kobject_t
    {
        private:
        static inline std::atomic_size_t next_id = 0;

        protected:
        device_t(std::string_view name, ktype_t &type, std::weak_ptr<kobject_t> parent)
            : kobject_t { name, type, parent },
              id { next_id.fetch_add(1, std::memory_order_relaxed) } { }

        public:
        const std::size_t id;

        bus_t *bus = nullptr;
        driver_t *drv = nullptr;
        class_t *cls = nullptr;

        dev_t devt = 0;
        std::shared_ptr<vfs::ops_t> fops;
        std::string modalias;

        bool probing = false;

        std::vector<std::pair<std::string, std::string>> props;

        template<typename ...Args>
        device_t(lib::private_t<device_t>, Args &&...args)
            : device_t { std::forward<Args>(args)... } { };

        device_t(const device_t &) = delete;
        device_t &operator=(const device_t &) = delete;

        static std::shared_ptr<device_t> create(
            std::string_view name, ktype_t &type, std::weak_ptr<kobject_t> parent
        )
        {
            return std::make_shared<device_t>(lib::private_t<device_t> { }, name, type, parent);
        }

        device_t *as_device() override { return this; }

        bool bound() const { return drv != nullptr; }

        lib::expect<void> emit(action act) override;
        std::string uevent_text() override;
    };

    class reflector_t
    {
        public:
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
    bool unregister_bus(bus_t &bus);

    lib::expect<void> register_class(class_t &cls);
    bool unregister_class(class_t &cls);

    lib::expect<void> register_driver(driver_t &drv);
    bool unregister_driver(driver_t &drv);

    lib::expect<void> bind_device(driver_t &drv, std::string_view name);
    lib::expect<void> unbind_device(driver_t &drv, std::string_view name);

    void probe_driver(driver_t &drv);

    lib::expect<void> uevent_store(kobject_t &kobj, std::string_view data);

    attribute_t &bind_attribute();
    attribute_t &unbind_attribute();

    lib::expect<void> register_device(std::shared_ptr<device_t> dev);
    bool unregister_device(std::shared_ptr<device_t> dev);

    lib::initgraph::stage *core_registered_stage();
    lib::initgraph::stage *available_stage();

    ktype_t &driver_ktype();
    ktype_t &bus_ktype();
    ktype_t &empty_ktype();

    std::shared_ptr<kobject_t> devices_root();
    std::shared_ptr<kobject_t> bus_root();
    std::shared_ptr<kobject_t> class_root();
    std::shared_ptr<kobject_t> dev_char_root();
    std::shared_ptr<kobject_t> dev_block_root();
    std::shared_ptr<kobject_t> virtual_root();
} // export namespace dev
