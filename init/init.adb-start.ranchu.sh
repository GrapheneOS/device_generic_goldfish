#!/system/bin/sh -e

# b/402444824: this enables Adb-over-USB which is wrong but this is all we have for now.
settings put global adb_enabled 1
