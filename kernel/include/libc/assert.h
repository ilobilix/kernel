// Copyright (C) 2024  ilobilo

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((noreturn))
void assert_fail(const char *message, const char *file, int line, const char *func);

#define ASSERT_MSG(x, msg) (!(x) ? assert_fail((msg), __FILE__, __LINE__, __PRETTY_FUNCTION__) : (void)((char *)(msg)))
#define ASSERT_NOMSG(x) (!(x) ? assert_fail("Assertion failed: " #x, __FILE__, __LINE__, __PRETTY_FUNCTION__) : (void)(0))
#define GET_MACRO(_1, _2, NAME, ...) NAME

#define assert(...) GET_MACRO(__VA_ARGS__, ASSERT_MSG, ASSERT_NOMSG)(__VA_ARGS__)

#ifdef __cplusplus
} // extern "C"
#endif
