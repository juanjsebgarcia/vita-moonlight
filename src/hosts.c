#include "i18n/i18n.h"
#include "hosts.h"

#include "check_host.h"
#include "config.h"
#include "debug.h"
#include "ui_ops.h"

#include <stdio.h>
#include <string.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

extern bool send_wol_packet(const char *mac, const char *ip_broadcast, int port);

// How long to wait for a PC to wake, how often to check, and how often to
// resend the packet (a PC can miss the first while its network card powers up)
#define WAKE_TIMEOUT_SECONDS 120
#define WAKE_CHECK_INTERVAL_US (2 * 1000 * 1000)
#define WAKE_RESEND_SECONDS 10

// The broadcast address of the host's /24: its IP with the last part as 255
static bool broadcast_address(const device_info_t *info, char *out, size_t size) {
  snprintf(out, size, "%s", info->internal);
  char *last_dot = strrchr(out, '.');
  // The dot, "255" and the terminator need five bytes from the dot on
  if (last_dot == NULL || (size_t)(last_dot - out) + 5 > size) {
    return false;
  }
  strcpy(last_dot + 1, "255");
  return true;
}

host_wake_result host_send_wake(const device_info_t *info, char *err, size_t err_len) {
  if (!info->mac[0]) {
    TR_FMT(err, err_len, STR_WAKE_NO_MAC, info->name);
    return HOST_WAKE_NO_MAC;
  }
  if (!info->internal[0]) {
    TR_FMT(err, err_len, STR_WAKE_NO_ADDRESS, info->name);
    return HOST_WAKE_NO_ADDRESS;
  }
  char broadcast[32];
  if (!broadcast_address(info, broadcast, sizeof(broadcast))) {
    TR_FMT(err, err_len, STR_WAKE_BAD_ADDRESS, info->name, info->internal);
    return HOST_WAKE_BAD_ADDRESS;
  }
  if (!send_wol_packet(info->mac, broadcast, 9)) {
    vita_debug_log("[HOSTS] Wake-on-LAN failed for %s (%s)", info->name, info->mac);
    TR_FMT(err, err_len, STR_WAKE_SEND_FAILED, info->name);
    return HOST_WAKE_SEND_FAILED;
  }
  vita_debug_log("[HOSTS] Wake-on-LAN sent to %s (%s)", info->name, info->mac);
  return HOST_WAKE_SENT;
}

host_wait_result host_wake_and_wait(device_info_t *info, char *new_address, size_t new_address_size,
                                    char *message, size_t message_size) {
  new_address[0] = '\0';
  message[0] = '\0';
  if (host_send_wake(info, message, message_size) != HOST_WAKE_SENT) {
    return HOST_WAIT_FAILED;
  }
  uint64_t start = sceKernelGetSystemTimeWide();
  uint64_t last_send = start;
  while (true) {
    int elapsed = (int)((sceKernelGetSystemTimeWide() - start) / 1000000);
    if (elapsed >= WAKE_TIMEOUT_SECONDS) {
      char minutes[8];
      snprintf(minutes, sizeof(minutes), "%d", WAKE_TIMEOUT_SECONDS / 60);
      TR_FMT(message, message_size, STR_WAKE_TIMED_OUT, info->name, minutes);
      return HOST_WAIT_TIMED_OUT;
    }
    char progress[256];
    char seconds[8];
    snprintf(seconds, sizeof(seconds), "%d", elapsed);
    TR_FMT(progress, sizeof(progress), STR_WAKE_WAITING, info->name, seconds);
    ui_progress(progress);
    // Nobody touches the Vita while waiting, so keep the screen on, as input would
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
    struct host_status st = check_host_status(info);
    if (st.status != HOST_OFFLINE) {
      vita_debug_log("[HOSTS] %s answered after %d s", info->name, elapsed);
      if (st.status == HOST_IP_CHANGED && st.current_ip[0] && strcmp(st.current_ip, info->internal) != 0) {
        // It woke up on a new address, for the caller to offer
        snprintf(new_address, new_address_size, "%s", st.current_ip);
      }
      return HOST_WAIT_ONLINE;
    }
    if (sceKernelGetSystemTimeWide() - last_send >= WAKE_RESEND_SECONDS * 1000000ULL) {
      char broadcast[32];
      if (broadcast_address(info, broadcast, sizeof(broadcast))) {
        send_wol_packet(info->mac, broadcast, 9);
      }
      last_send = sceKernelGetSystemTimeWide();
    }
    // Wait between checks, watching for cancel
    for (int i = 0; i < 20; i++) {
      if (ui_cancel_requested()) {
        TR_FMT(message, message_size, STR_WAKE_STOPPED, info->name);
        return HOST_WAIT_CANCELLED;
      }
      sceKernelDelayThread(WAKE_CHECK_INTERVAL_US / 20);
    }
  }
}

bool host_forget(device_info_t *info) {
  vita_debug_log("[HOSTS] forgetting %s", info->name);
  return remove_device(info->name);
}

void host_set_address(device_info_t *info, const char *address) {
  snprintf(info->internal, sizeof(info->internal), "%s", address);
  snprintf(info->external, sizeof(info->external), "%s", address);
  info->prefer_external = false;
  save_device_info(info);
}

void host_set_label(device_info_t *info, const char *label) {
  if (strcmp(label, info->name) == 0) {
    info->display_name[0] = '\0';
  } else {
    snprintf(info->display_name, sizeof(info->display_name), "%s", label);
  }
  save_device_info(info);
}

void host_remember_app(device_info_t *info, int app_id, const char *app_name) {
  // After every other PC's, so this one is the latest
  unsigned int latest = 0;
  for (int i = 0; i < known_devices.count; i++) {
    if (known_devices.devices[i].last_played > latest) {
      latest = known_devices.devices[i].last_played;
    }
  }
  if (info->last_played != latest || latest == 0) {
    info->last_played = latest + 1;
  }
  info->last_app_id = app_id;
  snprintf(info->last_app_name, sizeof(info->last_app_name), "%s", app_name);
  vita_debug_log("[HOSTS] last app on %s: %s (%d)", info->name, app_name, app_id);
  save_device_info(info);
}

device_info_t *host_last_played(void) {
  device_info_t *last = NULL;
  for (int i = 0; i < known_devices.count; i++) {
    device_info_t *d = &known_devices.devices[i];
    if (d->paired && d->last_played != 0 && d->last_app_name[0] != '\0' &&
        (last == NULL || d->last_played > last->last_played)) {
      last = d;
    }
  }
  return last;
}
