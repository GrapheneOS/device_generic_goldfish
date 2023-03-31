#ifndef __DEVICE_GOOGLE_GCE_WPA_SUPPLICANT_8_H__
#define __DEVICE_GOOGLE_GCE_WPA_SUPPLICANT_8_H__

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "wpa_supplicant_i.h"

#define LOG_TAG "GceWpaSupplicant8Driver"

#include "log/log.h"

#if GCE_WPA_SUPPLICANT_DEBUG
#  define D(...) ALOGD(__VA_ARGS__)
#else
#  define D(...) ((void)0)
#endif

typedef struct android_wifi_priv_cmd {
  char* buf;
  int used_len;
  int total_len;
} android_wifi_priv_cmd;

#endif  // __DEVICE_GOOGLE_GCE_WPA_SUPPLICANT_8_H__
