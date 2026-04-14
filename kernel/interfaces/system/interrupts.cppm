// Copyright (C) 2024-2025  ilobilo

export module system.interrupts;

import system.cpu;
import std;

export namespace interrupts
{
    class handler
    {
        private:
        std::function<void (cpu::registers *)> _handler;
        bool _reserved;

        public:
        handler() : _handler { }, _reserved { false } { }

        template<typename Func, typename ...Args>
        bool set(Func &&func, Args &&...args)
        {
            if (used())
                return false;

            _handler = [func = std::forward<Func>(func), ...args = std::forward<Args>(args)](cpu::registers *regs) mutable {
                func(regs, args...);
            };
            return true;
        }

        bool is_reserved() const
        {
            return _reserved == true;
        }

        bool reserve()
        {
            if (is_reserved())
                return false;

            return _reserved = true;
        }

        bool reset()
        {
            bool ret = static_cast<bool>(_handler);
            _handler = { };
            return ret;
        }

        bool reset_all()
        {
            _reserved = false;
            return reset();
        }

        bool used() const
        {
            return bool(_handler);
        }

        bool operator()(cpu::registers *regs)
        {
            if (used() == false)
                return false;

            _handler(regs);
            return true;
        }
    };

    std::optional<std::pair<handler &, std::size_t>> allocate(std::size_t cpuidx, std::size_t hint = 0);
    std::optional<std::reference_wrapper<handler>> get(std::size_t cpuidx, std::size_t vector);

    void mask(std::size_t vector);
    void unmask(std::size_t vector);
} // export namespace interrupts