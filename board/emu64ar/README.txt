The "emu64ar" product defines a non-hardware-specific IA target without a
kernel or bootloader.

This only supports 64-bit ABI and translated riscv64 ABI.

It can be used to build the entire user-level system, and will work with the
IA version of the emulator,

It is not a product "base class"; no other products inherit from it or use it
in any way.
