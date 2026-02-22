// Copyright (C) 2024-2026  ilobilo

export module drivers.output.serial;

export namespace output::serial
{
    struct printer
    {
        void (*printc)(char);
        printer *next;

        constexpr printer(void (*func)(char))
            : printc { func }, next { nullptr } { }
    };
    void register_printer(printer &prn);

    void printc(char chr);
} // export namespace output::serial