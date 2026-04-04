// Copyright (C) 2024-2026  ilobilo

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

inline int isalnum(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline int isalpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline int islower(int c)
{
    return (c >= 'a' && c <= 'z');
}

inline int isupper(int c)
{
    return (c >= 'A' && c <= 'Z');
}

inline int isdigit(int c)
{
    return (c >= '0' && c <= '9');
}

inline int isxdigit(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline int iscntrl(int c)
{
    return (c >= 0x00 && c <= 0x1F) || (c == 0x7F);
}

inline int isgraph(int c)
{
    return (c >= '!' && c <= '~');
}

inline int isspace(int c)
{
    return (c == ' ') || (c == '\f') || (c == '\n') || (c == '\r') || (c == '\t') || (c == '\v');
}

inline int isblank(int c)
{
    return (c == ' ') || (c == '\t');
}

inline int isprint(int c)
{
    return (c >= '!' && c <= '~') || (c == ' ');
}

inline int ispunct(int c)
{
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

inline int toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 0x20 : c;
}

inline int tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 0x20 : c;
}

#ifdef __cplusplus
} // extern "C"
#endif
