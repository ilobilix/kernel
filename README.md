# Ilobilix Kernel

Monolithic hobby kernel written in modern C++26 with modules. It aims for Linux ABI compatibility in userspace. This is a second rewrite started in 2024. Looking for contributors.

It has been tested with Gentoo (OpenRC), Void Linux and Alpine.

See [ilobilix/ilobilix](https://github.com/ilobilix/ilobilix) for more information and instructions on building and running the OS.

## License: [EUPL v1.2](LICENSE)

## Some Features
- C++ modules everywhere
- x86_64 and aarch64
- Loadable kernel modules
- Initgraph (from managarm)
- SMP, CFS-style scheduler
- Buddy PMM, UVM inspired VMM, ASIDs
- ACPI via uACPI
- Linux compatible syscalls, input subsystem, procfs, sysfs, etc
- UNIX sockets, netlink, pipes, ttys, ptys, VTs
- Networking via lwIP
- EXT2, SquashFS, TMPFS, USTAR and CPIO initrd
- NVMe, Virtio pci, input, net, entropy
- And more
