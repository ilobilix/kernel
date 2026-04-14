# Ilobilix
Hobby OS in modern C++

## License: [EUPL v1.2](LICENSE)

## Dependencies

Make sure you have recent versions of the following programs installed:
* [xmake](https://xmake.io/#/getting_started?id=installation)
* clang/clang++ (plus clang-scan-deps)
* llvm/lld
* xorriso
* qemu (x86_64 and aarch64)

## Building and Running

* Clone this repository: ``git clone --depth=1 https://github.com/ilobilo/ilobilix``
* Interactively configure the kernel: ``xmake f --menu``
  * Change architecture: ``xmake f --arch=<x86_64|aarch64>``
  * Change build mode: ``xmake f --mode=<release|releasesmall|releasedbg|debug>``
* Build and run the kernel: ``xmake run``
* Default run target is ``uefi``. Other possible values are: ``bios``, ``bios-debug`` and ``uefi-debug``. For example: ``xmake run bios-debug``
* Fully rebuild the kernel: ``xmake build -r --all -j$(nproc)``