// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.sched.wait_queue;
import system.chrono;

namespace sched
{
    namespace
    {
        struct workqueue_t
        {
            lib::spinlock_irq lock;

            struct entry_t
            {
                std::function<void ()> func;
                std::uint64_t deadline;
                lib::rbtree_hook<entry_t> hook;
            };

            lib::rbtree<
                entry_t,
                &entry_t::hook,
                lib::compare<
                    entry_t,
                    std::uint64_t,
                    &entry_t::deadline
                >
            > queue;

            wait_queue_t work_wq;
            thread_t *worker_thread = nullptr;

            [[noreturn]] static void worker(workqueue_t *self)
            {
                const auto timer = chrono::main_timer();
                while (true)
                {
                    self->lock.lock();
                    while (!self->queue.empty())
                    {
                        auto *first = self->queue.first();
                        const auto now = timer->ns();
                        if (first->deadline > now)
                        {
                            self->lock.unlock();
                            self->work_wq.wait(first->deadline - now);
                            self->lock.lock();
                            continue;
                        }
                        self->queue.remove(first);

                        lib::bug_on(!first->func);
                        self->lock.unlock();
                        first->func();
                        self->lock.lock();

                        delete first;
                    }
                    self->lock.unlock();
                    self->work_wq.wait();
                }
            }
        };

        workqueue_t wq;
    } // namespace

    lib::initgraph::stage *wq_initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "sched.work_queue.initialised",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task wq_task
    {
        "sched.work_queue.init",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            pid0_created_stage()
        },
        [] {
            wq.worker_thread = sched::spawn(workqueue_t::worker, &wq, 5);
        }
    };

    void schedule_work(std::function<void ()> func)
    {
        lib::bug_on(!func);
        {
            const std::unique_lock _ { wq.lock };
            wq.queue.insert(new workqueue_t::entry_t { std::move(func), 0, { } });
        }
        wq.work_wq.wake_one();
    }

    void schedule_work_after_ns(std::function<void ()> func, std::uint64_t ns)
    {
        lib::bug_on(!func);
        {
            const auto timer = chrono::main_timer();
            const auto now = timer->ns();
            const std::unique_lock _ { wq.lock };
            wq.queue.insert(new workqueue_t::entry_t { std::move(func), now + ns, { } });
        }
        wq.work_wq.wake_one();
    }
} // namespace sched
