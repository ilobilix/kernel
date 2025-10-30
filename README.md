# Ilobilix
Hobby OS in modern C++

## License: [EUPL v1.2](LICENSE)

## Dependencies

Make sure you have recent versions of the following programs installed:
* ``clang``/``clang++``
* ``lld``
* ``llvm``
* ``sed``
* ``tar``
* For creating a disk image
  * ``mtools`` (``mformat``, ``mmd`` and ``mcopy``)
  * ``parted``
  * ``coreutils`` (``truncate`` and ``dd``)
  * ``e2fsprogs`` (``mkfs.ext4``)
* For creating an iso image
  * ``xorriso``
* ``qemu-system`` (``x86_64`` and ``aarch64``)
* To build the sysroot:
  * [jinx dependencies](https://codeberg.org/Mintsuki/jinx#dependencies)

## Building and Running

* Clone this repository:
  * ``git clone --depth=1 --recursive https://github.com/ilobilo/ilobilix``
* (Optional) Configure the kernel:
  * ``./xmake.sh f --option=value``
  * Change architecture: ``./xmake.sh f --arch=[x86_64|aarch64]``
  * Change build mode: ``./xmake.sh f --mode=[release|releasedbg|debug]``
  * For all options see: ``./xmake.sh f --help``
* Build the sysroot:
  * ``./xmake.sh build sysroot``
* Build the kernel:
  * ``./xmake.sh build``
* Run the OS:
  * ``./xmake.sh run``
* (Optional) Run targets are ``[bios|uefi](-[debug|gdb])``. For example: ``bios-debug`` or ``uefi-gdb``
  * Default run target is ``uefi``. (same as ``./xmake.sh run uefi``)
* To build an iso (``build/ilobilix/[arch]/[mode]/image.iso``):
  * ``./xmake.sh build iso``

## Known Bugs
* ``aarch64`` basically doesn't work
* unconfirmed: sometimes sleeping thread doesn't wake up on bare metal