// Copyright (C) 2024-2025  ilobilo

module lib;

import system.scheduler;
import arch;
import std;

namespace lib
{
    lib::intrusive_list_hook<sched::thread_base> &semaphore::locate::operator()(sched::thread_base &x)
    {
        return static_cast<sched::thread *>(std::addressof(x))->semaphore_list_hook;
    }

    bool semaphore::test()
    {
        const bool ints = arch::int_switch_status(false);
        lock.lock();

        bool ret = false;
        if (signals > 0)
        {
            signals--;
            ret = true;
        }

        lock.unlock();
        arch::int_switch(ints);
        return ret;
    }

    bool semaphore::wait()
    {
        const bool ints = arch::int_switch_status(false);
        lock.lock();

        auto me = sched::this_thread();

        std::size_t reason = 0;
        if (--signals < 0)
        {
            threads.push_back(me);
            me->prepare_sleep();
            lock.unlock();

            if ((reason = sched::yield()))
            {
                lock.lock();
                if (const auto it = threads.find(me); it != threads.end())
                {
                    threads.remove(it);
                    signals++;
                }
                else reason = sched::wake_reason::success;

                lock.unlock();
            }
            arch::int_switch(ints);
            return reason == sched::wake_reason::success;
        }

        lock.unlock();
        arch::int_switch(ints);

        return reason == sched::wake_reason::success;
    }

    bool semaphore::wait_for(std::size_t ms)
    {
        do {
            if (test())
                return true;
            auto eep = std::min(static_cast<std::ssize_t>(ms), 10z);
            ms -= eep;

            if (eep)
                sched::sleep_for(eep);
        } while (ms);

        return false;
    }

    void semaphore::signal(bool drop)
    {
        const bool ints = arch::int_switch_status(false);
        lock.lock();

        if (drop && threads.size() == 0)
        {
            lock.unlock();
            arch::int_switch(ints);
            return;
        }

        sched::thread *thread = nullptr;
        if (++signals <= 0)
        {
            lib::bug_on(threads.size() == 0);
            thread = static_cast<sched::thread *>(threads.front());
            threads.pop_front();
        }

        lock.unlock();
        arch::int_switch(ints);

        if (thread)
            thread->wake_up(0);
    }

    void semaphore::signal_all()
    {
        const bool ints = arch::int_switch_status(false);
        lock.lock();

        bug_on(signals >= 0);
        auto temp_threads = std::move(threads);
        signals += static_cast<std::ssize_t>(temp_threads.size());

        lock.unlock();
        arch::int_switch(ints);

        for (auto &thread : temp_threads)
            static_cast<sched::thread *>(std::addressof(thread))->wake_up(0);
    }
} // namespace lib