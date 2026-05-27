# Ilobilix Kernel

Monolithic hobby kernel written in modern C++23. Boots on x86_64 and aarch64 with Limine. Aims for Linux ABI compatibility in userspace, supports loadable kernel modules. Looking for contributors.

It has been tested with Gentoo (OpenRC), Void Linux and Alpine.

See [ilobilix/ilobilix](https://github.com/ilobilix/ilobilix) for more information and instructions on building and running the OS.

## License: [EUPL v1.2](LICENSE)

## Notable Features
- C++ modules everywhere
- x86_64 and aarch64
- Loadable kernel modules
- Initgraph (from managarm)
- Buddy PMM, UVM inspired VMM, ASIDs
- ACPI via uACPI
- SMP, CFS-style scheduler
- Linux compatible syscalls, filesystems, etc
- UNIX sockets, pipes, ttys and ptys
- And more
