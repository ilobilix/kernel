// Copyright (C) 2024-2026  ilobilo

export module drivers.virtio:queue;

import libarch;
import lib;
import std;

import :spec;
import :transport;

export namespace virtio
{
    using cookie_t = std::uint64_t;

    using used_fn = std::function<void (cookie_t cookie, std::uint32_t len)>;
    using receive_fn = std::function<void (std::span<const std::byte> buffer)>;

    struct buffer_t
    {
        std::uintptr_t phys;
        std::uint32_t len;
    };

    class queue_t
    {
        friend class device_t;

        private:
        transport_t *_tp;

        virtq_desc *_desc;
        virtq_avail *_avail;
        virtq_used *_used;

        arch::dma_buffer_view _dma;
        arch::dma_buffer_view _buffer;

        used_fn _on_used;
        std::vector<cookie_t> _cookies;

        std::size_t _nbufs;
        std::size_t _bufsize;
        receive_fn _on_rx;

        lib::bitmap _posted;

        std::uint16_t _qid;
        std::uint16_t _size;

        std::uint16_t _free_head;
        std::uint16_t _num_free;
        std::uint16_t _last_used;

        bool _broken;

        mutable lib::spinlock_irq _lock;

        queue_t(
            transport_t &tp, std::uint16_t qid, std::uint16_t size,
            arch::dma_buffer_view dma, used_fn on_used,
            arch::dma_buffer_view buf, receive_fn on_receive,
            std::size_t nbufs, std::size_t bufsize
        );

        static lib::expect<std::unique_ptr<queue_t>> create(
            transport_t &tp, std::uint16_t qid, std::uint16_t size, used_fn on_used
        );

        static lib::expect<std::unique_ptr<queue_t>> create_buf(
            transport_t &tp, std::uint16_t qid, std::uint16_t size,
            std::size_t nbufs, std::size_t bufsize, receive_fn on_receive
        );

        std::byte *buffer_at(std::size_t index) const
        {
            return static_cast<std::byte *>(_buffer.data()) + (index * _bufsize);
        }

        cookie_t detach(std::uint16_t head);
        void repost(std::span<const std::pair<cookie_t, std::uint32_t>> done);

        queue_addr_t addr(std::uint16_t vector) const;
        std::size_t reap();

        public:
        queue_t(const queue_t &) = delete;
        queue_t &operator=(const queue_t &) = delete;

        ~queue_t();

        lib::expect<void> add(
            std::span<const buffer_t> drv_buf,
            std::span<const buffer_t> dev_buf,
            cookie_t cookie
        );

        void submit();

        std::uint16_t size() const { return _size; }
        std::uint16_t index() const { return _qid; }
        bool buffered() const { return _nbufs != 0; }

        std::uint16_t free_slots() const
        {
            const std::unique_lock _ { _lock };
            return _num_free;
        }
    };
} // export namespace virtio
