// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.sched.wait_queue;

namespace sched
{
    namespace
    {
        struct workqueue_t
        {
            lib::spinlock_irq lock;

            lib::list<std::function<void ()>> queue;

            wait_queue_t work_wq;
            thread_t *worker_thread = nullptr;

            [[noreturn]] static void worker(workqueue_t *self)
            {
                while (true)
                {
                    std::function<void ()> work;
                    {
                        const std::unique_lock _ { self->lock };
                        if (!self->queue.empty())
                        {
                            work = std::move(self->queue.front());
                            self->queue.pop_front();
                        }
                    }

                    if (work)
                        work();
                    else
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
            wq.worker_thread = sched::spawn(
                workqueue_t::worker,
                reinterpret_cast<std::uintptr_t>(&wq)
            );
        }
    };

    void schedule_work(std::function<void ()> func)
    {
        lib::bug_on(!func);
        {
            const std::unique_lock _ { wq.lock };
            wq.queue.push_back(std::move(func));
        }
        wq.work_wq.wake_one();
    }
} // namespace sched
