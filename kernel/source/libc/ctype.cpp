// Copyright (C) 2024-2025  ilobilo

extern "C"
{
    int isalnum(int c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    int isalpha(int c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    int islower(int c)
    {
        return (c >= 'a' && c <= 'z');
    }

    int isupper(int c)
    {
        return (c >= 'A' && c <= 'Z');
    }

    int isdigit(int c)
    {
        return (c >= '0' && c <= '9');
    }

    int isxdigit(int c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    int iscntrl(int c)
    {
        return (c >= 0x00 && c <= 0x1F) || (c == 0x7F);
    }

    int isgraph(int c)
    {
        return (c >= '!' && c <= '~');
    }

    int isspace(int c)
    {
        return (c == ' ') || (c == '\f') || (c == '\n') || (c == '\r') || (c == '\t') || (c == '\v');
    }

    int isblank(int c)
    {
        return (c == ' ') || (c == '\t');
    }

    int isprint(int c)
    {
        return (c >= '!' && c <= '~') || (c == ' ');
    }

    int ispunct(int c)
    {
        return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
    }

    int toupper(int c)
    {
        return (c >= 'a' && c <= 'z') ? c - 0x20 : c;
    }

    int tolower(int c)
    {
        return (c >= 'A' && c <= 'Z') ? c + 0x20 : c;
    }
} // extern "C"