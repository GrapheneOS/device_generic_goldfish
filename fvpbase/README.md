This document describes how to build and run an Android system image targeting
the ARM Fixed Virtual Platform.

### New to Android?

If you do not already have the ``repo`` tool, or a copy of the Android
source tree, please follow the Android instructions for [downloading the
source](https://source.android.com/setup/build/downloading).

### Building userspace

```
. build/envsetup.sh
lunch $TARGET
m
```

Where $TARGET above is one of: fvp-eng, fvp-userdebug, fvp_mini-eng or fvp_mini-userdebug
The fvp-* will build a full Android with UI support, while fvp_mini-* will build a small
subset needed to boot to shell and support command line executables.

It is possible to disable 32 bit support in fvp_mini-* targets with the following command
before ``m``:
`export FVP_MULTILIB_BUILD=false`

Note that running ``m`` requires that the kernel is built first following
the instructions below.

### Building the kernel

```
mkdir android-kernel-mainline
cd android-kernel-mainline
repo init -u https://android.googlesource.com/kernel/manifest -b common-android-mainline
repo sync
BUILD_CONFIG=common/build.config.gki.aarch64 build/build.sh -j72
BUILD_CONFIG=common-modules/virtual-device/build.config.fvp build/build.sh  -j72
```

The resulting kernel image and DTB must then be copied into the product output directory:

```
cp out/android-mainline/dist/Image $ANDROID_PRODUCT_OUT/kernel
cp out/android-mainline/dist/fvp-base-revc.dtb out/android-mainline/dist/initramfs.img $ANDROID_PRODUCT_OUT/
```

The above instructions currently only work for the ``fvp_mini``
target. If you would like to use the ``fvp`` target instead, the
following commands must be run after ``repo sync``:

```
repo start android-mainline common
repo download -c common 1463463
```

### Building the firmware (ARM Trusted Firmware and U-Boot)

First, install ``dtc``, the device tree compiler. On Debian, this is in the
``device-tree-compiler`` package. Then run:
```
mkdir platform
cd platform
repo init -u https://git.linaro.org/landing-teams/working/arm/manifest.git -m pinned-uboot.xml -b 20.01
repo sync

# The included copy of U-Boot is incompatible with this version of AOSP, switch to a recent upstream checkout.
cd u-boot
git fetch https://gitlab.denx.de/u-boot/u-boot.git/ master
git checkout 18b9c98024ec89e00a57707f07ff6ada06089d26
cd ..

mkdir -p tools/gcc
cd tools/gcc
wget https://releases.linaro.org/components/toolchain/binaries/6.2-2016.11/aarch64-linux-gnu/gcc-linaro-6.2.1-2016.11-x86_64_aarch64-linux-gnu.tar.xz
tar -xJf gcc-linaro-6.2.1-2016.11-x86_64_aarch64-linux-gnu.tar.xz
cd ../..

build-scripts/build-test-uboot.sh -p fvp all
```

These components must then be copied into the product output directory:

```
cp output/fvp/fvp-uboot/uboot/{bl1,fip}.bin $ANDROID_PRODUCT_OUT/
```

### Obtaining the model

The model may be obtained from [ARM's
website](https://developer.arm.com/tools-and-software/simulation-models/fixed-virtual-platforms)
(under "Armv8-A Base Platform FVP").

### Running the model

From a lunched environment, first set the value of the ``MODEL_BIN``
environment variable to the path to the model executable. Then run the
following command to launch the model:
```
device/generic/goldfish/fvpbase/run_model
```
Additional model parameters may be passed by appending them to the
``run_model`` command.

To terminate the model, press ``Ctrl-] Ctrl-D`` to terminate the telnet
connection.

### MTE support

By default only a small subset of platform binaries enables memory tagging. To
build everything with MTE, add ``export SANITIZE_TARGET=memtag_heap`` before
running ``m`` for Async mode, or ``export SANITIZE_TARGET=memtag_heap
SANITIZE_TARGET_DIAG=memtag_heap`` for Sync mode.

To launch the model with MTE support, the following additional parameters
must be used:
```
-C cluster0.has_arm_v8-5=1 \
-C cluster0.memory_tagging_support_level=2 \
-C bp.dram_metadata.is_enabled=1
```
MTE in userspace requires a patched common kernel with MTE support. To build
the kernel, follow the kernel instructions above, but before running the
``build.sh`` command, run:
```
cd common
git fetch https://github.com/pcc/linux android-experimental-mte
git checkout FETCH_HEAD
cd ..
```
Then replace the prebuilt binutils with binutils 2.33.1:
```
cd binutils-2.33.1
./configure --prefix=$PWD/inst --target=aarch64-linux-gnu
make
make install
ln -sf $PWD/inst/bin/aarch64-linux-gnu-as /path/to/android-kernel-mainline/prebuilts/gas/linux-x86/aarch64-linux-gnu-as
```

### Running the image in QEMU

As an alternative to using FVP, the image may also be run in QEMU.
QEMU is generally much faster than FVP, but its support for the
latest ARM architectural features is relatively new compared to FVP,
so it may have more bugs.

As of the time of writing, no released version of QEMU can successfully
boot the system to GUI due to bugs in its MTE support, so a development
version with bug fixes must be used. The instructions below check
out a commit that has been successfully tested.
```
git clone https://github.com/qemu/qemu
cd qemu
git checkout 9cd69f1a270235b652766f00b94114f48a2d603f
mkdir build
cd build
../configure --target-list=aarch64-softmmu
ninja
```
Then set the value of the ``QEMU_BIN`` environment variable to the path to
the resulting ``qemu-system-aarch64`` binary, and run the following command
in a lunched environment to start the emulator:
```
device/generic/goldfish/fvpbase/run_qemu
```
Additional QEMU arguments may be passed by appending them to the ``run_qemu``
command. One useful argument is ``-nographic``, which disables the GUI, which
may be useful when working with ``fvp_mini`` or if the GUI is not needed.

To terminate the emulator, press ``Ctrl-A c q <Enter>`` or close the GUI
window.

### Accessing the model via adb

To connect to the model on the host:
```
adb connect localhost:5555
```
