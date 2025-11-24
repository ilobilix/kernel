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
    } // namespace

    using namespace ::dev;

    instance::instance(driver *drv, std::uint32_t minor)
        : drv { drv }, minor { minor }, ref { 1 }
    {
        lib::bug_on(drv == nullptr);
        *termios.write_lock() = drv->init_termios;
    }

    struct test_driver : driver
    {
        struct test_instance : instance
        {
            bool open(std::shared_ptr<vfs::file> self) override
            {
                if (auto wlocked = ctrl.write_lock(); wlocked->pgid == 0 && wlocked->sid == 0)
                {
                    auto proc = sched::proc_for(self->pid);
                    wlocked->pgid = proc->pgid;
                    wlocked->sid = proc->sid;
                }
                return true;
            }

            bool close() override { return true; }

            std::ssize_t read(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
            {
                lib::unused(offset, buffer);
                arch::halt();
                return 0;
            }

            std::ssize_t write(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
            {
                lib::unused(offset);
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
                : instance { drv, minor } { }
        };

        std::shared_ptr<instance> create_instance(std::uint32_t minor) override
        {
            auto wlocked = instances.write_lock();
            if (wlocked->contains(minor))
                return nullptr;

            auto [it, inserted] = wlocked->insert({ minor, std::make_shared<test_instance>(this, minor) });
            if (!inserted)
                return nullptr;
            log::debug("tty: created test instance with minor {}", minor);
            return it->second;
        }

        void destroy_instance(std::shared_ptr<instance> inst) override
        {
            lib::bug_on(!inst);
            lib::bug_on(!instances.write_lock()->erase(inst->minor));
            log::debug("tty: destroyed test instance with minor {}", inst->minor);
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
                auto rlocked = drv->instances.read_lock();
                auto it = rlocked->find(minor(rdev));
                if (it != rlocked->end())
                {
                    inst = it->second;
                    inst->ref.fetch_add(1);
                }
            }

            if (!inst)
            {
                inst = drv->create_instance(minor(rdev));
                if (!inst)
                    return (errno = ENODEV, false);
                if (!inst->open(self))
                {
                    drv->destroy_instance(inst);
                    return false;
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
            if (!inst->close())
                return false;

            if (inst->ref.fetch_sub(1) == 1)
            {
                lib::bug_on(!inst->drv);
                inst->drv->destroy_instance(inst);
            }

            const auto rdev = self->path.dentry->inode->stat.st_rdev;
            log::debug("tty: closed ({}, {}) for pid {}", major(rdev), minor(rdev), self->pid);
            return true;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::bug_on(!file || !file->private_data);
            auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->read(offset, buffer);
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::bug_on(!file || !file->private_data);
            auto inst = std::static_pointer_cast<instance>(file->private_data);
            return inst->write(offset, buffer);
        }

        int ioctl(std::shared_ptr<vfs::file> file, unsigned long request, lib::uptr_or_addr argp) override
        {
            lib::bug_on(!file || !file->private_data);
            auto inst = std::static_pointer_cast<instance>(file->private_data);
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
            auto test_drv = new test_driver { };
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
                if (!ret)
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