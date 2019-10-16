#!/vendor/bin/sh -e

insmod /vendor/lib/modules/fb.ko
insmod /vendor/lib/modules/cfbcopyarea.ko
insmod /vendor/lib/modules/cfbfillrect.ko
insmod /vendor/lib/modules/cfbimgblt.ko
insmod /vendor/lib/modules/amba-clcd.ko

insmod /vendor/lib/modules/fixed.ko
insmod /vendor/lib/modules/mmc_core.ko
insmod /vendor/lib/modules/mmc_block.ko
insmod /vendor/lib/modules/armmmci.ko

insmod /vendor/lib/modules/ambakmi.ko
insmod /vendor/lib/modules/mousedev.ko
insmod /vendor/lib/modules/psmouse.ko

setprop vendor.all.modules.ready 1
