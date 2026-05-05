// Copyright (C) 2024-2026  ilobilo

#ifndef ILOBILIX_GTHREAD_SHIM_H
#define ILOBILIX_GTHREAD_SHIM_H

#define __GTHREADS 1
#define __GTHREAD_HAS_COND 1

#define __GTHREAD_MUTEX_INIT 0
#define __GTHREAD_RECURSIVE_MUTEX_INIT 0
#define __GTHREAD_COND_INIT 0

#define __gthread_cond_t int

#define __gthread_active_p() 1

#define __gthread_mutex_lock(m)              ((void)(m), 0)
#define __gthread_mutex_unlock(m)            ((void)(m), 0)
#define __gthread_mutex_destroy(m)           ((void)(m), 0)
#define __gthread_recursive_mutex_lock(m)    ((void)(m), 0)
#define __gthread_recursive_mutex_unlock(m)  ((void)(m), 0)
#define __gthread_recursive_mutex_destroy(m) ((void)(m), 0)
#define __gthread_cond_broadcast(c)          ((void)(c), 0)
#define __gthread_cond_wait(c, m)            ((void)(c), (void)(m), 0)
#define __gthread_cond_wait_recursive(c, m)  ((void)(c), (void)(m), 0)
#define __gthread_cond_destroy(c)            ((void)(c), 0)

#endif
