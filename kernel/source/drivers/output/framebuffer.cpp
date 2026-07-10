// Copyright (C) 2024-2026  ilobilo

module;

#include <limine.h>

module drivers.output.framebuffer;

import drivers.fs.devtmpfs;
import drivers.fs.sysfs;
import system.memory.virt;
import system.vfs.dev;
import system.vfs;
import system.dev;
import system.pci;
import boot;
import lib;
import fmt;

namespace output::frm
{
    void init()
    {
        const auto frms = boot::requests::framebuffer.response->framebuffers;
        const auto num = boot::requests::framebuffer.response->framebuffer_count;

        lib::info("available framebuffers: {}", num);

        for (std::size_t i = 0; i < num; i++)
        {
            const auto &entry = frms[i];
            auto &frm = framebuffers.emplace_back(*entry);

            frm.edid = new std::byte[entry->edid_size];
            std::memcpy(frm.edid, entry->edid, entry->edid_size);

            frm.modes = new limine_video_mode *[frm.mode_count];
            for (std::size_t ii = 0; ii < frm.mode_count; ii++)
                frm.modes[ii] = new limine_video_mode(*entry->modes[ii]);
        }
    }

    namespace
    {
        struct fb_fix_screeninfo
        {
            char id[16];
            unsigned long smem_start;

            std::uint32_t smem_len;
            std::uint32_t type;
            std::uint32_t type_aux;
            std::uint32_t visual;
            std::uint16_t xpanstep;
            std::uint16_t ypanstep;
            std::uint16_t ywrapstep;
            std::uint32_t line_length;
            unsigned long mmio_start;

            std::uint32_t mmio_len;
            std::uint32_t accel;

            std::uint16_t capabilities;
            std::uint16_t reserved[2];
        };

        struct fb_bitfield
        {
            std::uint32_t offset;
            std::uint32_t length;
            std::uint32_t msb_right;
        };

        struct fb_var_screeninfo
        {
            std::uint32_t xres;
            std::uint32_t yres;
            std::uint32_t xres_virtual;
            std::uint32_t yres_virtual;
            std::uint32_t xoffset;
            std::uint32_t yoffset;

            std::uint32_t bits_per_pixel;
            std::uint32_t grayscale;

            fb_bitfield red;
            fb_bitfield green;
            fb_bitfield blue;
            fb_bitfield transp;

            std::uint32_t nonstd;

            std::uint32_t activate;

            std::uint32_t height;
            std::uint32_t width;

            std::uint32_t accel_flags;

            std::uint32_t pixclock;
            std::uint32_t left_margin;
            std::uint32_t right_margin;
            std::uint32_t upper_margin;
            std::uint32_t lower_margin;
            std::uint32_t hsync_len;
            std::uint32_t vsync_len;
            std::uint32_t sync;
            std::uint32_t vmode;
            std::uint32_t rotate;
            std::uint32_t colorspace;
            std::uint32_t reserved[4];
        };

        struct fb_cmap
        {
            std::uint32_t start;
            std::uint32_t len;
            std::uint16_t *red;
            std::uint16_t *green;
            std::uint16_t *blue;
            std::uint16_t *transp;
        };

        enum fb_blank
        {
            fb_blank_unblank = 0,
            fb_blank_normal = 1,
            fb_blank_vsync_suspend = 2,
            fb_blank_hsync_suspend = 3,
            fb_blank_powerdown = 4
        };

        struct [[gnu::packed]] edid_info
        {
            uint8_t padding[8];
            uint16_t manufacturer_id_be;
            uint16_t edid_id_code;
            uint32_t serial_num;
            uint8_t man_week;
            uint8_t man_year;
            uint8_t edid_version;
            uint8_t edid_revision;
            uint8_t video_input_type;
            uint8_t max_hor_size;
            uint8_t max_ver_size;
            uint8_t gamma_factor;
            uint8_t dpms_flags;
            uint8_t chroma_info[10];
            uint8_t est_timings1;
            uint8_t est_timings2;
            uint8_t man_res_timing;
            uint16_t std_timing_id[8];
            uint8_t det_timing_desc1[18];
            uint8_t det_timing_desc2[18];
            uint8_t det_timing_desc3[18];
            uint8_t det_timing_desc4[18];
            uint8_t unused;
            uint8_t checksum;
        };

        struct fb_dev : vfs::ops_t
        {
            fb_fix_screeninfo fix;
            fb_var_screeninfo var;
            vmm::object::ptr mmap_obj;

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file_t> file,
                std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(file);
                if (buffer.size() == 0 || offset > fix.smem_len)
                    return 0uz;

                const auto to_copy = std::min(buffer.size(), fix.smem_len - offset);
                if (to_copy == 0)
                    return 0;

                if (!buffer.subspan(0, to_copy).copy_from(
                    reinterpret_cast<std::byte *>(lib::tohh(fix.smem_start) + offset)))
                    return std::unexpected { lib::err::invalid_address };
                return to_copy;
            }

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file_t> file,
                std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(file);
                if (buffer.size() == 0 || offset > fix.smem_len)
                    return 0uz;

                const auto to_copy = std::min(buffer.size(), fix.smem_len - offset);
                if (to_copy == 0)
                    return 0;

                if (!buffer.subspan(0, to_copy).copy_to(
                    reinterpret_cast<std::byte *>(lib::tohh(fix.smem_start) + offset)))
                    return std::unexpected { lib::err::invalid_address };
                return to_copy;
            }

            lib::expect<int> ioctl(
                std::shared_ptr<vfs::file_t> file, std::uint64_t request,
                lib::uptr_or_addr argp
            ) override
            {
                lib::unused(file);
                switch (request)
                {
                    case 0x4600: // FBIOGET_VSCREENINFO
                        if (!argp.write(var))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    case 0x4601: // FBIOPUT_VSCREENINFO
                        if (!argp.read(var))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    case 0x4602: // FBIOGET_FSCREENINFO
                        if (!argp.write(fix))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    case 0x4605: // FBIOPUTCMAP
                    {
                        fb_cmap cmap;
                        if (!argp.read(cmap))
                            return std::unexpected { lib::err::invalid_address };
                        if (cmap.len == 0)
                            return std::unexpected { lib::err::invalid_argument };
                        return 0;
                    }
                    case 0x4606: // FBIOPAN_DISPLAY
                    {
                        fb_var_screeninfo pan;
                        if (!argp.read(pan))
                            return std::unexpected { lib::err::invalid_address };

                        if (pan.xoffset + var.xres > var.xres_virtual ||
                            pan.yoffset + var.yres > var.yres_virtual)
                            return std::unexpected { lib::err::invalid_argument };

                        var.xoffset = pan.xoffset;
                        var.yoffset = pan.yoffset;
                        return 0;
                    }
                    case 0x4611: // FBIOBLANK
                    {
                        switch (argp.address())
                        {
                            case fb_blank_unblank:
                            case fb_blank_normal:
                            case fb_blank_vsync_suspend:
                            case fb_blank_hsync_suspend:
                            case fb_blank_powerdown:
                                return 0;
                            default:
                                return std::unexpected { lib::err::invalid_argument };
                        }
                    }
                    default:
                        lib::error("fbdev: unhandled ioctl: 0x{:X}", request);
                        break;
                }
                return std::unexpected { lib::err::inappropriate_ioctl };
            }

            lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file_t> file) override
            {
                lib::unused(file);
                return mmap_obj;
            }
        };

        struct attribute_t : dev::attribute_t
        {
            lib::expect<std::string> (*fn)(dev::device_t &, fb_dev &);

            attribute_t(decltype(fn) fn, std::string_view name, mode_t mode)
                : dev::attribute_t { name, mode }, fn { fn } { }

            lib::expect<std::string> show(dev::kobject_t &kobj) override
            {
                const auto device = kobj.as_device();
                if (!device || !device->fops)
                    return std::unexpected { lib::err::io_error };
                return fn(*device, *static_cast<fb_dev *>(device->fops.get()));
            }
        };

        struct ktype_t : dev::ktype_t
        {
            std::span<dev::attribute_t> attributes() const override
            {
                static dev::attribute_t list[] {
                    attribute_t {
                        [](dev::device_t &, fb_dev &fb) -> lib::expect<std::string> {
                            return std::string {
                                fb.fix.id, std::strnlen(fb.fix.id, sizeof(fb.fix.id))
                            } + '\n';
                        }, "name", 0444
                    },
                    attribute_t {
                        [](dev::device_t &device, fb_dev &) -> lib::expect<std::string> {
                            using namespace vfs::dev;
                            return fmt::format("{}:{}\n", major(device.devt), minor(device.devt));
                        }, "dev", 0444
                    },
                    attribute_t {
                        [](dev::device_t &, fb_dev &fb) -> lib::expect<std::string> {
                            return std::to_string(fb.var.bits_per_pixel) + '\n';
                        }, "bits_per_pixel", 0444
                    },
                    attribute_t {
                        [](dev::device_t &, fb_dev &fb) -> lib::expect<std::string> {
                            return fmt::format("{},{}\n", fb.var.xres_virtual, fb.var.yres_virtual);
                        }, "virtual_size", 0444
                    },
                    attribute_t {
                        [](dev::device_t &, fb_dev &fb) -> lib::expect<std::string> {
                            return std::to_string(fb.fix.line_length) + '\n';
                        }, "stride", 0444
                    }
                };
                return list;
            }
        } ktype;

        lib::initgraph::task fbdev_task
        {
            "vfs.dev.fbdev.register",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require {
                dev::available_stage(),
                pci::registered_stage()
            },
            [] {
                // TODO: move this somewhere else
                struct graphics_class_t final : dev::class_t
                {
                    graphics_class_t() : dev::class_t { "graphics", dev::empty_ktype(), false } { }

                    std::string devnode(const dev::device_t &dev, mode_t &mode) const override
                    {
                        lib::unused(mode);
                        return dev.name;
                    }
                };
                static graphics_class_t cls;
                if (const auto ret = dev::register_class(cls); !ret)
                    lib::error("fbdev: failed to register class 'graphics'");

                for (std::size_t i = 0; const auto &fb : framebuffers)
                {
                    std::uint32_t height = -1, width = -1;
                    if (fb.edid)
                    {
                        const auto edid = reinterpret_cast<edid_info *>(fb.edid);
                        if (edid->max_hor_size && edid->max_ver_size)
                        {
                            width = edid->max_hor_size * 10;
                            height = edid->max_ver_size * 10;
                        }
                    }

                    auto ops = std::make_shared<fb_dev>();

                    auto &fix = ops->fix;
                    {
                        // TODO
                        std::memcpy(fix.id, "fb", sizeof("fb"));
                        fix.smem_start = reinterpret_cast<std::uintptr_t>(lib::fromhh(fb.address));
                        fix.smem_len = fb.pitch * fb.height;
                        fix.type = 0; // FB_TYPE_PACKED_PIXELS
                        fix.type_aux = 0;
                        fix.visual = 2; // FB_VISUAL_TRUECOLOR
                        fix.xpanstep = 0;
                        fix.ypanstep = 0;
                        fix.ywrapstep = 0;
                        fix.line_length = fb.pitch;
                        fix.mmio_len = 0;
                        fix.mmio_len = 0;
                        fix.accel = 0; // FB_ACCEL_NONE
                        fix.capabilities = 0;
                    }

                    auto &var = ops->var;
                    {
                        var.xres = fb.width;
                        var.yres = fb.height;
                        var.xres_virtual = fb.width;
                        var.yres_virtual = fb.height;
                        var.xoffset = 0;
                        var.yoffset = 0;
                        var.bits_per_pixel = fb.bpp;
                        var.grayscale = 0;
                        var.red = fb_bitfield {
                            .offset = fb.red_mask_shift,
                            .length = fb.red_mask_size,
                            .msb_right = 0
                        };
                        var.green = fb_bitfield {
                            .offset = fb.green_mask_shift,
                            .length = fb.green_mask_size,
                            .msb_right = 0
                        };
                        var.blue = fb_bitfield {
                            .offset = fb.blue_mask_shift,
                            .length = fb.blue_mask_size,
                            .msb_right = 0
                        };
                        var.transp = fb_bitfield { };
                        var.nonstd = 0;
                        var.activate = 0;
                        var.height = height;
                        var.width = width;
                        var.accel_flags = 0;
                        var.pixclock = 0;
                        var.left_margin = 0;
                        var.right_margin = 0;
                        var.upper_margin = 0;
                        var.lower_margin = 0;
                        var.hsync_len = 0;
                        var.vsync_len = 0;
                        var.sync = 0;
                        var.vmode = 0;
                        var.rotate = 0;
                        var.colorspace = 0;
                    }

                    {
                        const auto npsize = vmm::pagemap::from_page_size(vmm::default_page_size());
                        const auto pages = lib::div_roundup<std::size_t>(fix.smem_len, npsize);
                        ops->mmap_obj = vmm::object::ptr {
                            new vmm::pmemobject {
                                fix.smem_start, pages, vmm::caching::framebuffer
                            }
                        };
                    }

                    // TODO: temporary workaround to make xorg happy
                    std::shared_ptr<pci::device_t> found;
                    for (const auto &device : pci::get_bus()->get_devices())
                    {
                        auto dev = static_cast<pci::device_t *>(device.get())->dev;
                        if (dev->class_ != 0x03)
                            continue;

                        for (const auto &bar : dev->get_bars())
                        {
                            if (bar.type == pci::bar::type::mem && bar.phys <= fix.smem_start &&
                                bar.phys + bar.size >= fix.smem_start + fix.smem_len)
                            {
                                found = std::static_pointer_cast<pci::device_t>(device);
                                break;
                            }
                        }
                    }

                    std::shared_ptr<dev::kobject_t> root;
                    if (found)
                    {
                        root = dev::kobject_t::create(
                            "graphics", dev::empty_ktype(), found
                        );
                        lib::bug_on(!dev::register_kobject(root));
                    }
                    else root = dev::virtual_root();

                    auto device = dev::device_t::create(
                        "fb" + std::to_string(i), ktype, root
                    );
                    device->cls = &cls;
                    device->devt = vfs::dev::makedev(29, i);
                    device->fops = std::move(ops);

                    if (const auto ret = dev::register_device(std::move(device)); !ret)
                    {
                        lib::panic(
                            "fbdev: failed to register 'fb{}': {}",
                            i, lib::error_name(ret.error())
                        );
                    }

                    i++;
                }
            }
        };
    } // namespace
} // namespace output::frm
