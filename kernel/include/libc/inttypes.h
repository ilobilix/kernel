// Copyright (C) 2024  ilobilo

#pragma once

#if defined(__x86_64__) || defined(__aarch64__)
#  define __PRI64_PREFIX "l"
#else
#  define __PRI64_PREFIX "ll"
#endif

#define PRIu64 __PRI64_PREFIX "u"
#define PRIx64 __PRI64_PREFIX "x"
#define PRIX64 __PRI64_PREFIX "X"
