#!/system/bin/sh -e
DEVICE_STATE_FILE="/data/vendor/device_state_configuration.xml"
STATE=`getprop ro.boot.qemu.device_state`

if [ -n "$STATE" ]; then
  echo "$STATE" > "$DEVICE_STATE_FILE"
  chmod 0755 "$DEVICE_STATE_FILE"
fi
