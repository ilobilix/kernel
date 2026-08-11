// Copyright (C) 2024-2026  ilobilo

module system.virtio;

namespace virtio
{
    namespace
    {
        constexpr std::size_t reap_batch = 32;
        arch::contiguous_pool pool { };

        template<typename Fn>
        bool walk_queue(virtq_desc *desc, std::uint16_t size, std::uint16_t head, Fn &&fn)
        {
            auto cur = head;
            for (std::uint16_t seen = 0; seen < size; seen++)
            {
                const auto next = desc[cur].next;
                const bool more = desc[cur].flags & flag::desc_next;

                fn(cur);
                if (!more)
                    return true;

                if (next >= size)
                    return false;

                cur = next;
            }
            return false;
        }
    } // namespace

    queue_t::queue_t(
        transport_t &tp, std::uint16_t qid, std::uint16_t size,
        arch::dma_buffer_view dma, used_fn on_used,
        arch::dma_buffer_view buf, receive_fn on_receive,
        std::size_t nbufs, std::size_t bufsize
    ) : _tp { std::addressof(tp) }, _dma { dma }, _buffer { buf },
        _on_used { std::move(on_used) }, _cookies { },
        _nbufs { nbufs }, _bufsize { bufsize }, _on_rx { std::move(on_receive) },
        _posted { }, _qid { qid }, _size { size }, _free_head { 0 }, _num_free { size },
        _last_used { 0 }, _broken { false }, _lock { }
    {
        const auto layout = get_layout(size, tp.legacy_layout());
        auto base = static_cast<std::byte *>(_dma.data());

        _desc = std::start_lifetime_as<virtq_desc>(base);
        _avail = std::start_lifetime_as<virtq_avail>(base + layout.avail_off);
        _used = std::start_lifetime_as<virtq_used>(base + layout.used_off);

        for (std::uint16_t i = 0; i < size; i++)
            _desc[i].next = i + 1;

        if (!buffered())
        {
            _cookies.resize(size);
            return;
        }

        _posted.initialise(_nbufs);

        for (std::uint16_t i = 0; i < _nbufs; i++)
        {
            _desc[i].addr = lib::fromhh(reinterpret_cast<std::uintptr_t>(buffer_at(i)));
            _desc[i].len = _bufsize;
            _desc[i].flags = flag::desc_write;
            _desc[i].next = 0;

            _avail->ring[i] = i;
            _posted.set(i, true);
        }

        _num_free = 0;
        std::atomic_ref { _avail->idx } .store(_nbufs, std::memory_order_release);
    }

    queue_t::~queue_t()
    {
        if (_dma.size() != 0)
            pool.deallocate(_dma.get_dma_ptr(), _dma.size(), 1, alignment);

        if (_buffer.size() != 0)
            pool.deallocate(_buffer.get_dma_ptr(), _buffer.size(), 1, alignment);
    }

    lib::expect<std::unique_ptr<queue_t>> queue_t::create(
        transport_t &tp, std::uint16_t qid, std::uint16_t size, used_fn on_used
    )
    {
        if (size == 0 || !std::has_single_bit(size))
            return std::unexpected { lib::err::invalid_argument };

        const auto layout = get_layout(size, tp.legacy_layout());
        auto dma = pool.allocate(layout.size, 1, alignment);
        if (!dma)
            return std::unexpected { lib::err::out_of_memory };

        return std::unique_ptr<queue_t> {
            new queue_t {
                tp, qid, size,
                arch::dma_buffer_view { dma, layout.size },
                std::move(on_used), { }, { }, 0, 0
            }
        };
    }

    lib::expect<std::unique_ptr<queue_t>> queue_t::create_buf(
        transport_t &tp, std::uint16_t qid, std::uint16_t size,
        std::size_t nbufs, std::size_t bufsize, receive_fn on_receive
    )
    {
        if (size == 0 || !std::has_single_bit(size))
            return std::unexpected { lib::err::invalid_argument };

        if (nbufs == 0 || nbufs > size)
            return std::unexpected { lib::err::invalid_argument };

        if (bufsize == 0 || bufsize > std::numeric_limits<std::uint32_t>::max())
            return std::unexpected { lib::err::invalid_argument };

        if (!on_receive)
            return std::unexpected { lib::err::invalid_argument };

        const auto total = nbufs * bufsize;
        if (total / nbufs != bufsize)
            return std::unexpected { lib::err::invalid_argument };

        const auto layout = get_layout(size, tp.legacy_layout());
        auto dma = pool.allocate(layout.size, 1, alignment);
        if (!dma)
            return std::unexpected { lib::err::out_of_memory };

        auto buf = pool.allocate(total, 1, alignment);
        if (!buf)
        {
            pool.deallocate(dma, layout.size, 1, alignment);
            return std::unexpected { lib::err::out_of_memory };
        }

        return std::unique_ptr<queue_t> {
            new queue_t {
                tp, qid, size,
                arch::dma_buffer_view { dma, layout.size }, { },
                arch::dma_buffer_view { buf, total }, std::move(on_receive),
                nbufs, bufsize
            }
        };
    }

    queue_addr_t queue_t::addr(std::uint16_t vector) const
    {
        return {
            lib::fromhh(reinterpret_cast<std::uintptr_t>(_desc)),
            lib::fromhh(reinterpret_cast<std::uintptr_t>(_avail)),
            lib::fromhh(reinterpret_cast<std::uintptr_t>(_used)),
            _size, vector
        };
    }

    cookie_t queue_t::detach(std::uint16_t head)
    {
        std::uint16_t tail = head;
        std::uint16_t count = 0;

        if (!walk_queue(_desc, _size, head, [&](std::uint16_t i) { tail = i; count++; }))
        {
            _broken = true;
            return 0;
        }

        _desc[tail].next = _free_head;
        _free_head = head;
        _num_free += count;

        return std::exchange(_cookies[head], 0);
    }

    void queue_t::repost(std::span<const std::pair<cookie_t, std::uint32_t>> done)
    {
        const std::unique_lock _ { _lock };
        if (_broken)
            return;

        auto idx_ref = std::atomic_ref { _avail->idx };
        auto idx = idx_ref.load(std::memory_order_relaxed);

        for (const auto &[index, _] : done)
        {
            _avail->ring[idx++ % _size] = index;
            _posted.set(index, true);
        }

        idx_ref.store(idx, std::memory_order_release);
    }

    std::size_t queue_t::reap()
    {
        const bool is_buffered = buffered();

        std::array<std::pair<cookie_t, std::uint32_t>, reap_batch> batch;
        std::size_t total = 0;

        while (true)
        {
            std::size_t count = 0;
            {
                const std::unique_lock _ { _lock };
                if (_broken)
                    break;

                const auto limit = is_buffered ? _nbufs : _size;
                const auto idx = std::atomic_ref { _used->idx } .load(std::memory_order_acquire);

                while (_last_used != idx && count < batch.size())
                {
                    const auto &elem = _used->ring[_last_used % _size];
                    const std::uint16_t head = elem.id;

                    if (head >= limit)
                        _broken = true;
                    else if (is_buffered)
                    {
                        if (_posted.set(head, false))
                        {
                            batch[count++] = {
                                head, std::min<std::uint32_t>(elem.len, _bufsize)
                            };
                        }
                        else _broken = true;
                    }
                    else
                    {
                        const auto cookie = detach(head);
                        if (!_broken)
                            batch[count++] = { cookie, elem.len };
                    }

                    if (_broken)
                    {
                        lib::error("virtio: queue {} used ring is corrupted", _qid);
                        break;
                    }

                    _last_used++;
                }
            }

            if (count == 0)
                break;

            if (const auto done = std::span { batch } .first(count); is_buffered)
            {
                for (const auto &[index, len] : done)
                    _on_rx({ buffer_at(index), len });
                repost(done);
            }
            else if (_on_used)
            {
                for (const auto &[cookie, len] : done)
                    _on_used(cookie, len);
            }
            total += count;
        }

        if (is_buffered && total != 0 && !_broken)
            submit();
        return total;
    }

    lib::expect<void> queue_t::add(
        std::span<const buffer_t> drv_buf,
        std::span<const buffer_t> dev_buf,
        cookie_t cookie
    )
    {
        if (buffered())
            return std::unexpected { lib::err::not_supported };

        const auto needed = drv_buf.size() + dev_buf.size();
        if (needed == 0)
            return std::unexpected { lib::err::invalid_argument };

        if (needed > _size)
            return std::unexpected { lib::err::no_buffer_space };

        const std::unique_lock _ { _lock };

        if (_broken)
            return std::unexpected { lib::err::io_error };

        if (needed > _num_free)
            return std::unexpected { lib::err::try_again };

        const auto head = _free_head;
        auto cur = head;
        auto prev = head;

        const auto put = [&](const buffer_t &buf, std::uint16_t extra) {
            _desc[cur].addr = buf.phys;
            _desc[cur].len = buf.len;
            _desc[cur].flags = extra;

            prev = cur;
            cur = _desc[cur].next;
        };

        for (std::size_t i = 0; i < drv_buf.size(); i++)
            put(drv_buf[i], flag::desc_next);

        for (std::size_t i = 0; i < dev_buf.size(); i++)
            put(dev_buf[i], flag::desc_next | flag::desc_write);

        _desc[prev].flags &= ~flag::desc_next;

        _free_head = cur;
        _num_free -= needed;
        _cookies[head] = cookie;

        auto idx_ref = std::atomic_ref { _avail->idx };
        const auto idx = idx_ref.load(std::memory_order_relaxed);
        _avail->ring[idx % _size] = head;
        idx_ref.store(idx + 1, std::memory_order_release);
        return { };
    }

    void queue_t::submit()
    {
        std::atomic_thread_fence(std::memory_order_seq_cst);

        const auto flags = std::atomic_ref { _used->flags } .load(std::memory_order_relaxed);
        if (!(flags & flag::used_no_notify))
            _tp->notify(_qid);
    }
} // namespace virtio
