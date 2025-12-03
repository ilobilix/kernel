// Copyright (C) 2024-2025  ilobilo

module lib;

import system.scheduler;
import arch;
import std;

namespace lib
{
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

    void semaphore::wait()
    {
        const bool ints = arch::int_switch_status(false);
        lock.lock();

        auto me = sched::this_thread();

        if (--signals < 0)
        {
            threads.push_back(me);
            me->prepare_sleep();
            lock.unlock();

            if (sched::yield())
            {
                lock.lock();
                auto it = std::remove(threads.begin(), threads.end(), me);
                if (it != threads.end())
                {
                    threads.erase(it);
                    signals++;
                }
                lock.unlock();
            }
            arch::int_switch(ints);
            return;
        }

        lock.unlock();
        arch::int_switch(ints);
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
} // namespace lib