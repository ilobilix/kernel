// Copyright (C) 2024-2026  ilobilo

export module drivers.output.serial;

export namespace output::serial
{
    struct logger
    {
        void (*printc)(char);
        logger *next;

        constexpr logger(void (*func)(char))
            : printc { func }, next { nullptr } { }
    };
    void register_logger(logger &prn);

    void printc(char chr);
} // export namespace output::serial