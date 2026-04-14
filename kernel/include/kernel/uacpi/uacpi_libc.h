// Copyright (C) 2024  ilobilo

#pragma once

#include <stdio.h>
#include <string.h>

#define uacpi_memcpy memcpy
#define uacpi_memmove memmove
#define uacpi_memset memset
#define uacpi_memcmp memcmp
#define uacpi_strcmp strcmp
#define uacpi_strnlen strnlen
#define uacpi_strlen strlen
#define uacpi_snprintf snprintf
#define uacpi_vsnprintf vsnprintf