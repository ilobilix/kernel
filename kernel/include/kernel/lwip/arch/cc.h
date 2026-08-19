// Copyright (C) 2024-2026  ilobilo

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_UNISTD_H   1

#define X8_F  "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "zu"

#include <limits.h>
#ifndef SSIZE_MAX
#  define SSIZE_MAX LONG_MAX
#endif

#define BYTE_ORDER LITTLE_ENDIAN

typedef unsigned long sys_prot_t;

unsigned int lwip_port_rand(void);
#define LWIP_RAND() (lwip_port_rand())

int *lwip_port_errno(void);
#define errno (*lwip_port_errno())

#ifdef __cplusplus
} // extern "C"
#endif
