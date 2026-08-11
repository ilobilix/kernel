// Copyright (C) 2024-2026  ilobilo

export module system.virtio:spec;

import lib;
import std;

export namespace virtio
{
    enum class device_type : std::uint32_t
    {
        invalid = 0,
        network = 1,
        block = 2,
        console = 3,
        entropy_source = 4,
        memory_ballooning = 5,
        io_memory = 6,
        rpmsg = 7,
        scsi_host = 8,
        transport_9p = 9,
        mac80211_wlan = 10,
        rproc_serial = 11,
        virtio_caif = 12,
        memory_balloon = 13,
        gpu = 16,
        clock = 17,
        input = 18,
        socket = 19,
        crypto = 20,
        signal_distribution_module = 21,
        pstore = 22,
        iommu = 23,
        memory = 24,
        sound = 25,
        filesystem = 26,
        pmem = 27,
        rpmb = 28,
        mac80211_hwsim = 29,
        video_encoder = 30,
        video_decoder = 31,
        scmi = 32,
        nitrosecuremodule = 33,
        i2c_adapter = 34,
        watchdog = 35,
        can = 36,
        parameter_server = 38,
        audio_policy = 39,
        bluetooth = 40,
        gpio = 41,
        rdma = 42,
        camera = 43,
        ism = 44,
        spi_master = 45
    };

    enum status : std::uint8_t
    {
        acknowledge = 1,
        driver = 2,
        driver_ok = 4,
        features_ok = 8,
        needs_reset = 0x40,
        failed = 0x80
    };

    enum feature : std::uint32_t
    {
        notify_on_empty = 24,
        any_layout = 27,
        indirect_desc = 28,
        event_idx = 29,
        version_1 = 32,
        access_platform = 33,
        ring_packed = 34,
        in_order = 35,
        order_platform = 36,
        sr_iov = 37,
        notification_data = 38,
        notif_config_data = 39,
        ring_reset = 40,
        admin_vq = 41
    };

    constexpr std::uint64_t feature_bit(feature feat)
    {
        return 1ul << std::to_underlying(feat);
    }

    enum flag : std::uint16_t
    {
        used_no_notify = 1,
        avail_no_interrupt = 1,

        desc_next = 1,
        desc_write = 2,
        desc_indirect = 4,
    };

    struct virtq_desc
    {
        std::uint64_t addr;
        std::uint32_t len;
        std::uint16_t flags;
        std::uint16_t next;
    };
    static_assert(sizeof(virtq_desc) == 16);

    struct virtq_avail
    {
        std::uint16_t flags;
        std::uint16_t idx;
        std::uint16_t ring[];
        // if f_event_idx
        // std::uint16_t used_event;
    };

    struct virtq_used_elem
    {
        std::uint32_t id;
        std::uint32_t len;
    };

    struct virtq_used
    {
        std::uint16_t flags;
        std::uint16_t idx;
        virtq_used_elem ring[];
        // if f_event_idx
        // std::uint16_t avail_event;
    };

    constexpr std::size_t alignment = 0x1000;

    struct vring_layout
    {
        std::size_t avail_off;
        std::size_t used_off;
        std::size_t size;
    };

    constexpr vring_layout get_layout(std::uint16_t size, bool legacy)
    {
        const auto desc = sizeof(virtq_desc) * size;
        const auto avail = sizeof(std::uint16_t) * (3 + size);
        const auto used_off = lib::align_up(
            desc + avail, legacy ? alignment : alignof(virtq_used_elem)
        );

        return {
            desc, used_off,
            used_off + (sizeof(std::uint16_t) * 3) + (sizeof(virtq_used_elem) * size)
        };
    }
} // export namespace virtio
