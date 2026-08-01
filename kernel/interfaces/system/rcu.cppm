// Copyright (C) 2024-2026  ilobilo

export module system.rcu;

import system.cpu.local;
import lib;
import std;

// non-preemptible rcu
// can not sleep in read sections

namespace rcu
{
    constinit std::atomic_bool ready = false;
} // namespace rcu

export namespace rcu
{
    struct alignas(64) cpu_state_t
    {
        std::atomic<std::size_t> nesting = 0;
        std::atomic<std::uint64_t> qs_seq = 0;
        std::atomic<bool> need_qs = false;
    };

    cpu_state_t &get_state();
    void report_qs_slow();
    void synchronise();
    void init_cpu();

    inline void read_lock()
    {
        lib::lock::acquire_preempt();
        if (!ready.load(std::memory_order_relaxed)) [[unlikely]]
            return;

        auto &st = get_state();
        st.nesting.store(
            st.nesting.load(std::memory_order_relaxed) + 1,
            std::memory_order_relaxed
        );
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }

    inline void read_unlock()
    {
        if (ready.load(std::memory_order_relaxed)) [[likely]]
        {
            auto &st = get_state();
            std::atomic_signal_fence(std::memory_order_seq_cst);

            if (const auto depth = st.nesting.load(std::memory_order_relaxed)) [[likely]]
            {
                st.nesting.store(depth - 1, std::memory_order_relaxed);
                if (depth == 1 && st.need_qs.load(std::memory_order_relaxed)) [[unlikely]]
                    report_qs_slow();
            }
        }
        lib::lock::release_preempt();
    }

    inline std::size_t nesting()
    {
        if (!ready.load(std::memory_order_relaxed)) [[unlikely]]
            return 0;
        return get_state().nesting.load(std::memory_order_relaxed);
    }

    inline void note_context_switch()
    {
        if (!ready.load(std::memory_order_relaxed)) [[unlikely]]
            return;
        report_qs_slow();
    }

    struct head_t
    {
        head_t *next;
        void (*func)(head_t *self);
    };

    void queue_callback(head_t *head);
    void barrier();

    class domain_t
    {
        protected:
        constexpr domain_t() = default;
        ~domain_t() = default;

        public:
        domain_t(const domain_t &) = delete;
        domain_t &operator=(const domain_t &) = delete;

        virtual void lock() = 0;
        virtual bool try_lock() = 0;
        virtual void unlock() = 0;

        virtual void synchronise() = 0;
        virtual void barrier() = 0;
        virtual void retire(head_t *head) = 0;
    };

    domain_t &default_domain();

    inline void synchronise(domain_t &dom) { dom.synchronise(); }
    inline void barrier(domain_t &dom) { dom.barrier(); }

    struct read_guard
    {
        read_guard() { read_lock(); }
        ~read_guard() { read_unlock(); }

        read_guard(const read_guard &) = delete;
        read_guard &operator=(const read_guard &) = delete;
    };

    template<typename Type, typename Deleter = std::default_delete<Type>>
    class obj_base
    {
        private:
        head_t _head;
        [[no_unique_address]] Deleter _deleter;

        protected:
        constexpr obj_base() : _head { }, _deleter { } { }

        constexpr obj_base(const obj_base &other)
            : _head { }, _deleter { other._deleter } { }
        constexpr obj_base(obj_base &&other)
            : _head { }, _deleter { std::move(other._deleter) } { }

        obj_base &operator=(const obj_base &other)
        {
            _deleter = other._deleter;
            return *this;
        }

        obj_base &operator=(obj_base &&other)
        {
            _deleter = std::move(other._deleter);
            return *this;
        }

        ~obj_base() = default;

        public:
        void retire(Deleter deleter = Deleter { }, domain_t &dom = default_domain())
        {
            static_assert(std::is_standard_layout_v<obj_base>);
            static_assert(std::derived_from<Type, obj_base>);

            lib::bug_on(_head.func != nullptr);

            _deleter = std::move(deleter);
            _head.func = [](head_t *head) {
                const auto self = reinterpret_cast<obj_base *>(head);
                auto deleter = std::move(self->_deleter);
                deleter(static_cast<Type *>(self));
            };
            dom.retire(&_head);
        }
    };

    template<typename Type>
        requires (std::is_class_v<Type> && !std::is_final_v<Type>)
    class box : public Type, public obj_base<box<Type>>
    {
        public:
        constexpr box() = default;

        constexpr box(const Type &value) : Type { value } { }
        constexpr box(Type &&value) : Type { std::move(value) } { }
    };

    template<typename Type>
    class pointer
    {
        private:
        std::atomic<Type *> _ptr;

        public:
        constexpr pointer(Type *ptr = nullptr)
            : _ptr { ptr } { }

        pointer(const pointer &) = delete;
        pointer &operator=(const pointer &) = delete;

        // can only be called from read section
        Type *dereference() const
        {
            return _ptr.load(std::memory_order_acquire);
        }

        void assign(Type *ptr)
        {
            _ptr.store(ptr, std::memory_order_release);
        }

        Type *exchange(Type *ptr)
        {
            return _ptr.exchange(ptr, std::memory_order_acq_rel);
        }

        // for writers
        Type *unsafe_load() const
        {
            return _ptr.load(std::memory_order_relaxed);
        }
    };

    template<typename Type>
        requires std::derived_from<Type, obj_base<Type>>
    class owner : public pointer<Type>
    {
        public:
        using pointer<Type>::pointer;

        ~owner()
        {
            if (const auto ptr = this->unsafe_load())
                ptr->retire();
        }
    };

    template<typename Type>
        requires std::derived_from<Type, obj_base<Type>>
    class updater
    {
        private:
        pointer<Type> &_ptr;
        Type *_old;
        Type *_next;
        domain_t &_dom;
        bool _committed;

        public:
        updater(pointer<Type> &ptr, domain_t &dom = default_domain())
            : _ptr { ptr }, _old { ptr.unsafe_load() },
              _next { _old ? new Type { *_old } : new Type },
              _dom { dom }, _committed { false } { }

        ~updater()
        {
            if (!_committed)
                delete _next;
        }

        updater(const updater &) = delete;
        updater &operator=(const updater &) = delete;

        Type *operator->() const { return _next; }
        Type &operator*() const { return *_next; }
        Type *get() const { return _next; }

        const Type *previous() const { return _old; }

        void commit()
        {
            lib::bug_on(_committed == true);

            _ptr.assign(_next);
            if (_old)
                _old->retire({ }, _dom);

            _committed = true;
        }
    };
} // export namespace rcu
