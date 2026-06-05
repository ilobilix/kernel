// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.sysfs;

import lib;

export namespace fs::sysfs
{
    lib::initgraph::stage *registered_stage();
    lib::initgraph::stage *mounted_stage();
} // export namespace fs::sysfs
