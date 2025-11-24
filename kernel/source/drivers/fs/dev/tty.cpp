// Copyright (C) 2024-2025  ilobilo

module drivers.fs.dev.tty;

import drivers.fs.devtmpfs;
import system.memory.virt;
import system.scheduler;
import system.vfs;
import system.dev;
import arch;
import lib;
import cppstd;

import magic_enum;
import fmt;

namespace fs::dev::tty
{
    namespace
    {
        frg::intrusive_list<
            driver,
            frg::locate_member<
                driver,
                frg::default_list_hook<driver>,
                &driver::hook
            >
        > drivers;

        driver *get_driver(dev_t major)
        {
            for (const auto &drv : drivers)
            {
                if (drv->major == major)
                    return drv;
            }
            return nullptr;
        }

        bool generic_open(std::shared_ptr<vfs::file> self, std::shared_ptr<instance> inst)
        {
            auto wlocked = inst->ctrl.write_lock();
            lib::bug_on(wlocked->pgid != 0 || wlocked->sid != 0);

            auto proc = sched::proc_for(self->pid);
            if (!proc)
                return (errno = ESRCH, false);
            wlocked->pgid = proc->pgid;
            wlocked->sid = proc->sid;

            return inst->open(self);
        }
    } // namespace

    using namespace ::dev;

    std::ssize_t default_ldisc::read(lib::maybe_uspan<std::byte> buffer)
    {
        lib::unused(buffer);
        return -1;
    }

    std::ssize_t default_ldisc::write(lib::maybe_uspan<std::byte> buffer)
    {
        lib::bug_on(!inst);
        return inst->transmit(buffer);
    }

    void default_ldisc::receive(std::span<std::byte> buffer)
    {
        lib::unused(buffer);
    }

    instance::instance(driver *drv, std::uint32_t minor, std::unique_ptr<line_discipline> ldisc)
        : drv { drv }, minor { minor }, ref { 0 }, ldisc { std::move(ldisc) },
          termios { drv->init_termios }, winsize { }, ctrl { }
    { lib::bug_on(drv == nullptr); }

    struct test_driver : driver
    {
        struct test_instance : instance
        {
            bool open(std::shared_ptr<vfs::file> self) override
            {
                lib::unused(self);
                return true;
            }

            bool close() override { return true; }

            std::ssize_t transmit(lib::maybe_uspan<std::byte> buffer) override
            {
                lib::membuffer buf { buffer.size_bytes() };
                buffer.copy_to(buf.span());
                const std::string_view str { reinterpret_cast<const char *>(buf.data()), buf.size_bytes() };
                log::print("{}", str);
                return buffer.size_bytes();
            }

            void flush_buffer() override { }

            int ioctl(unsigned long request, lib::uptr_or_addr argp) override
            {
                lib::unused(request, argp);
                return 0;
            }

            test_instance(driver *drv, std::uint32_t minor)
                : instance { drv, minor, std::make_unique<default_ldisc>(this) } { }
        };

        std::shared_ptr<instance> create_instance(std::uint32_t minor) override
        {
            log::debug("tty: creating test instance with minor {}", minor);
            return std::make_shared<test_instance>(this, minor);
        }

        void destroy_instance(std::shared_ptr<instance> inst) override
        {
            log::debug("tty: destroying test instance with minor {}", inst->minor);
        }

        int ioctl(std::shared_ptr<instance> inst, unsigned long request, lib::uptr_or_addr argp) override
        {
            lib::bug_on(!inst);
            return inst->ioctl(request, argp);
        }

        test_driver() : driver { "tty-test", 4, 0, termios { } } { }
    };

    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        bool open(std::shared_ptr<vfs::file> self) override
        {
            lib::bug_on(self->private_data != nullptr);
            lib::bug_on(!self || !self->path.dentry || !self->path.dentry->inode);

            const auto rdev = self->path.dentry->inode->stat.st_rdev;
            auto drv = get_driver(major(rdev));
            if (!drv)
                return (errno = ENODEV, false);

            std::shared_ptr<instance> inst;
            {
                auto locked = drv->instances.lock();
                if (auto it = locked->find(minor(rdev)); it != locked->end())
                {
                    // found an already open instance
                    inst = it->second;
                    inst->ref.fetch_add(1, std::memory_order_acq_rel);
                }
                else
                {
                    inst = drv->create_instance(minor(rdev));
                    if (!inst)
                        return (errno = ENODEV, false);

                    if (!generic_open(self, inst))
                    {
                        drv->destroy_instance(inst);
                        return false;
                    }
                    inst->ref.store(1, std::memory_order_relaxed);
                    locked->emplace(minor(rdev), inst);
                }
            }
            self->private_data = inst;

            log::debug("tty: opened ({}, {}) for pid {}", major(rdev), minor(rdev), self->pid);
            return true;
        }

        bool close(std::shared_ptr<vfs::file> self) override
        {
            lib::bug_on(!self || !self->private_data);
            const auto inst = std::static_pointer_cast<instance>(self->private_data);

            const auto prev = inst->ref.fetch_sub(1, std::memory_order_acq_rel);
            lib::bug_on(prev == 0);
            if (prev == 1)
            {
                const auto drv = inst->drv;
                lib::bug_on(drv == nullptr);
                {
                    auto locked = drv->instances.lock();
                    if (inst->ref.load(std::memory_order_acquire) != 0)
                        return true;

                    if (!inst->close())
                    {
                        inst->ref.fetch_add(1, std::memory_order_relaxed);
                        return false;
                    }
                    lib::bug_on(!locked->erase(inst->minor));
                }
                drv->destroy_instance(inst);
            }
            self->private_data.reset();

            const auto rdev = self->path.dentry->inode->stat.st_rdev;
            log::debug("tty: closed ({}, {}) for pid {}", major(rdev), minor(rdev), self->pid);
            return true;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->read(buffer);
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(offset);
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->write(buffer);
        }

        int ioctl(std::shared_ptr<vfs::file> file, unsigned long request, lib::uptr_or_addr argp) override
        {
            lib::bug_on(!file || !file->private_data);
            const auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->ioctl(request, argp);
        }

        bool trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return true;
        }

        std::shared_ptr<vmm::object> map(std::shared_ptr<vfs::file> file, bool priv) override
        {
            lib::unused(file, priv);
            return nullptr;
        }

        bool sync() override { return true; }
    };

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.dev.tty-registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task tty_task
    {
        "vfs.dev.tty.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { devtmpfs::mounted_stage() },
        lib::initgraph::entail { registered_stage() },
        [] {
            const auto test_drv = new test_driver { };
            drivers.push_back(test_drv);

            const auto add_test_tty = [&]
            {
                static std::size_t minor = 0;
                register_cdev(ops::singleton(), makedev(test_drv->major, minor));

                const auto name = fmt::format("/dev/tty{}", minor);
                auto ret = vfs::create(
                    std::nullopt, name, stat::s_ifchr | 0666,
                    makedev(test_drv->major, minor)
                );

                minor++;
                if (!ret.has_value())
                {
                    log::error(
                        "tty: could not create '{}': {}",
                        name, magic_enum::enum_name(ret.error())
                    );
                }
            };

            for (std::size_t i = 0; i < 4; i++)
                add_test_tty();
        }
    };
} // namespace fs::dev::tty