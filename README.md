# Ilobilix
Hobby OS in modern C++

## License: [EUPL v1.2](LICENSE)

## Building and Running

Make sure you have the following programs installed:
* xmake
* clang/clang++ (tested versions >= 18)
* clang-tools (for clang-scan-deps)
* lld
* llvm
* xorriso
* tar
* xbstrap
* qemu-system-x86_64
* qemu-system-aarch64

Note: you will need more packages to build the sysroot, such as ``flex bison automake autoconf autopoint gperf help2man texinfo libgmp-dev libmpc-dev libmpfr-dev`` etc

* If you are on a Debian based system you can install some of the required packages with this command:\
``sudo apt install xorriso tar qemu-system-x86 qemu-system-arm``
* Install llvm, lld, clang and clang-tools from: https://apt.llvm.org
* To install ``xmake``, simply follow instructions on this link: https://xmake.io/#/getting_started?id=installation
* For ``xbstrap``, make sure you have python pip installed, then run: ``python -m pip install xbstrap``

Follow these steps to build and run the os:
1. Clone this repository with: ``git clone --depth=1 https://github.com/ilobilo/ilobilix``
2. Manually build the sysroot:
   * Set the architecture in ``userspace/boostrap.yml``
   * ``mkdir build-sysroot``
   * ``pushd build-sysroot``
   * ``xbstrap init ../userspace``
   * ``xbstrap install base``
   * If symlink named ``sysroot`` does not exist in ``$PROJECT_ROOT/userspace`` that links to ``$BUILD_SYSROOT_DIR/system-root``, create it with:\
   ``ln -s $BUILD_SYSROOT_DIR/system-root $PROJECT_ROOT/userspace/sysroot``\
   For example, run this command from project root: \
   ``ln -s ../build-sysroot/system-root userspace/sysroot``
   * ``popd``
3. Optionally configure the kernel with: ``xmake f --menu -y``
4. To switch between architectures: ``xmake f --arch=<arch> -y``
5. To change build modes: ``xmake f -m <release/releasesmall/releasedbg/debug> -y``
6. Build and run the kernel: ``xmake build -j$(nproc) && xmake run`` or just ``xmake run -j$(nproc)``
7. Default run target is ``uefi``. Possible values are: ``bios`` ``bios-debug`` ``uefi`` and ``uefi-debug``. For example: ``xmake run bios-debug``
8. Rebuild the kernel if configuration is changed: ``xmake build -r --all -j$(nproc)``