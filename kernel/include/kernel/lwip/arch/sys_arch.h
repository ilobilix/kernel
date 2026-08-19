// Copyright (C) 2024-2026  ilobilo

#pragma once

#include <lwip/arch.h>
#include <lwip/err.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lwip_port_sem_t;
struct lwip_port_mutex_t;
struct lwip_port_mbox_t;
struct lwip_port_thread_t;

typedef struct lwip_port_sem_t *sys_sem_t;
typedef struct lwip_port_mutex_t *sys_mutex_t;
typedef struct lwip_port_mbox_t *sys_mbox_t;
typedef struct lwip_port_thread_t *sys_thread_t;

#define SYS_SEM_NULL  ((sys_sem_t)0)
#define SYS_MBOX_NULL ((sys_mbox_t)0)

#define sys_sem_valid(sem_p)           (*(sem_p) != SYS_SEM_NULL)
#define sys_sem_set_invalid(sem_p)     do { *(sem_p) = SYS_SEM_NULL; } while (0)
#define sys_sem_valid_val(sem)         ((sem) != SYS_SEM_NULL)
#define sys_sem_set_invalid_val(sem)   do { (sem) = SYS_SEM_NULL; } while (0)

#define sys_mbox_valid(mbox_p)         (*(mbox_p) != SYS_MBOX_NULL)
#define sys_mbox_set_invalid(mbox_p)   do { *(mbox_p) = SYS_MBOX_NULL; } while (0)
#define sys_mbox_valid_val(mbox)       ((mbox) != SYS_MBOX_NULL)
#define sys_mbox_set_invalid_val(mbox) do { (mbox) = SYS_MBOX_NULL; } while (0)

#define sys_mutex_valid(mu_p)          (*(mu_p) != NULL)
#define sys_mutex_set_invalid(mu_p)    do { *(mu_p) = NULL; } while (0)

void sys_init(void);
u32_t sys_now(void);

sys_prot_t sys_arch_protect(void);
void sys_arch_unprotect(sys_prot_t pval);

void sys_msleep(u32_t ms);

err_t sys_sem_new(sys_sem_t *sem, u8_t count);
void sys_sem_signal(sys_sem_t *sem);
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout);
void sys_sem_free(sys_sem_t *sem);

err_t sys_mutex_new(sys_mutex_t *mutex);
void sys_mutex_lock(sys_mutex_t *mutex);
void sys_mutex_unlock(sys_mutex_t *mutex);
void sys_mutex_free(sys_mutex_t *mutex);

err_t sys_mbox_new(sys_mbox_t *mbox, int size);
void sys_mbox_post(sys_mbox_t *mbox, void *msg);
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg);
err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg);
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout);
u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg);
void sys_mbox_free(sys_mbox_t *mbox);

sys_thread_t sys_thread_new(
    const char *name, void (*thread)(void *arg),
    void *arg, int stacksize, int prio
);

#ifdef __cplusplus
} // extern "C"
#endif
