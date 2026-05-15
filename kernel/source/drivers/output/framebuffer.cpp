// Copyright (C) 2024-2026  ilobilo

module;

#include <limine.h>

module drivers.output.framebuffer;

import drivers.fs.devtmpfs;
import system.memory.virt;
import system.vfs.dev;
import system.vfs;
import boot;
import lib;

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

        struct fb_dev : vfs::ops
        {
            fb_fix_screeninfo fix;
            fb_var_screeninfo var;
            vmm::object::ptr mmap_obj;

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file> file,
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
                std::shared_ptr<vfs::file> file,
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

            lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
            {
                lib::unused(file, size);
                return { };
            }

            lib::expect<int> ioctl(
                std::shared_ptr<vfs::file> file, std::uint64_t request,
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
                    default:
                        lib::error("fbdev: unhandled ioctl: 0x{:X}", request);
                        break;
                }
                return std::unexpected { lib::err::inappropriate_ioctl };
            }

            lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file> file) override
            {
                lib::unused(file);
                return mmap_obj;
            }
        };

        lib::initgraph::task fbdev_task
        {
            "vfs.dev.fbdev.register",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require { fs::devtmpfs::registered_stage() },
            [] {
                using namespace vfs::dev;

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
                        // fix.id = ;
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

                    register_ops(makedev(29, i), std::move(ops));

                    const auto name = "fb" + std::to_string(i);
                    const auto ret = fs::devtmpfs::create(
                        name,
                        stat::s_ifchr | 0660,
                        makedev(29, i)
                    );
                    if (!ret && ret.error() != lib::err::already_exists)
                    {
                        lib::panic(
                            "fbdev: failed to create '/dev/{}': {}",
                            name, lib::error_name(ret.error())
                        );
                    }

                    i++;
                }
            }
        };
    } // namespace
} // namespace output::frm
