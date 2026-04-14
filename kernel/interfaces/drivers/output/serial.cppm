// Copyright (C) 2024-2025  ilobilo

export module drivers.output.serial;

export namespace output::serial
{
    using printer = void (*)(char);
    void register_printer(printer prn);

    void printc(char chr);
} // export namespace output::serial