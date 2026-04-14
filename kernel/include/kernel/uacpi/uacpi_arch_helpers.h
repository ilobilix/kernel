// Copyright (C) 2024-2025  ilobilo

#pragma once

#if defined(__x86_64__)
#  define UACPI_ARCH_FLUSH_CPU_CACHE() __asm__ volatile ("wbinvd")
#elif defined(__aarch64__)
#  define UACPI_ARCH_FLUSH_CPU_CACHE() do { } while (0)
#endif

typedef unsigned long uacpi_cpu_flags;
typedef void *uacpi_thread_id;

#define UACPI_ATOMIC_LOAD_THREAD_ID(ptr) ((uacpi_thread_id)uacpi_atomic_load_ptr(ptr))
#define UACPI_ATOMIC_STORE_THREAD_ID(ptr, value) uacpi_atomic_store_ptr(ptr, value)
#define UACPI_THREAD_ID_NONE ((uacpi_thread_id)-1)