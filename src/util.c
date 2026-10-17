#include "util.h"
#include "debug.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/net/netctl.h>
#include <psp2/registrymgr.h>

void swap_app_list_entries(PAPP_LIST a, PAPP_LIST b) {
    PAPP_LIST tmp = malloc(sizeof(APP_LIST));
    tmp->id = a->id;
    tmp->name = a->name;

    a->id = b->id;
    a->name = b->name;
    b->id = tmp->id;
    b->name = tmp->name;

    free(tmp);
}

void sort_app_list(PAPP_LIST list) {
    if (list == NULL) {
        return;
    }

    int swapped = 0;
    PAPP_LIST cur = NULL;
    PAPP_LIST prev = NULL;

    do {
        swapped = 0;
        cur = list;

        while (cur->next != prev) {
            if (strcmp(cur->name, cur->next->name) > 0) {
                swap_app_list_entries(cur, cur->next);
                swapped = 1;
            }
            cur = cur->next;
        }
        prev = cur;
    } while (swapped);
}

int ensure_buf_size(void **buf, size_t *buf_size, size_t required_size) {
  if (*buf_size >= required_size)
    return 0;

  *buf_size = required_size;
  *buf = realloc(*buf, *buf_size);
  if (!*buf) {
    fprintf(stderr, "Failed to allocate %zu bytes\n", *buf_size);
    abort();
  }

  return 0;
}

bool vita_bluetooth_enabled(void) {
  // The switch in Settings > Bluetooth Devices, as the system stores it
  int enabled = 0;
  return sceRegMgrGetKeyInt("/CONFIG/BT", "bt_enable", &enabled) >= 0 && enabled != 0;
}

int vita_wifi_percent(void) {
  SceNetCtlInfo info;
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &info) < 0) {
    return -1;
  }
  return info.rssi_percentage;
}

// SceNetCtlInfo.device has no named values in VitaSDK. 0 is Wi-Fi
// (SCE_NET_CTL_DEVICE_WIRELESS, as Vita3K returns it, and as a handheld on
// Wi-Fi reports). The other values aren't
// public, but the hardware settles it: a PS TV has Ethernet and no modem, and
// a 3G Vita has a modem and no Ethernet.
#define NETCTL_DEVICE_WIFI 0

int vita_network_type(void) {
  SceNetCtlInfo info;
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_DEVICE, &info) < 0) {
    return VITA_NETWORK_NONE;
  }
  if (info.device == NETCTL_DEVICE_WIFI) {
    return VITA_NETWORK_WIFI;
  }
  return sceKernelGetModelForCDialog() == SCE_KERNEL_MODEL_VITATV ? VITA_NETWORK_WIRED : VITA_NETWORK_MOBILE;
}
