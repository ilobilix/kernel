// Copyright (C) 2024-2026  ilobilo

#pragma once

#ifndef __GNUC_VA_LIST
#  define __GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif

typedef __builtin_va_list va_list;

#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_copy(dst, src) __builtin_va_copy(dst, src)
#define __va_copy(dst, src) __builtin_va_copy(dst, src)
