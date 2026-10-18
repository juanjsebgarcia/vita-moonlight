#include "i18n/i18n.h"
#include "hosts.h"

#include "check_host.h"
#include "config.h"
#include "connection.h"
#include "debug.h"
#include "session.h"
#include "ui_ops.h"

#include "errors.h"

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
  // remove_device shifts the list over info before it deletes the files by
  // name, so the name must not point into the list
  char name[sizeof(info->name)];
  snprintf(name, sizeof(name), "%s", info->name);
  return remove_device(name);
}

// Whether gs_unpair's result came from the PC, rather than from not reaching it
static bool pc_answered(int ret) {
  return ret == GS_OK || ret == GS_ERROR || ret == GS_INVALID;
}

// Asks the PC to forget this Vita. Returns whether it answered at all, and
// sets told when it agreed (or had already forgotten it).
static bool ask_pc_to_unpair(device_info_t *info, bool *told) {
  *told = false;
  int status = connection_get_status();
  if (status != LI_DISCONNECTED) {
    if (strcmp(session_server_name(), info->name) != 0) {
      // Connected to another PC: opening this one would replace that
      // connection's keys under it, so this PC isn't asked
      vita_debug_log("[HOSTS] not asking %s to unpair: connected to %s", info->name, session_server_name());
      return false;
    }
    // Already connected to it, so ask over that connection, then end it
    int ret = gs_unpair(&server);
    vita_debug_log("[HOSTS] unpair request to %s: %d", info->name, ret);
    *told = ret == GS_OK;
    if (status == LI_READY) {
      connection_paired();
    }
    connection_terminate();
    return pc_answered(ret);
  }

  // gs_init keeps a pointer to the address, so it can't be on the stack
  static char address[sizeof(info->internal)];
  struct host_status st = check_host_status(info);
  if (st.status != HOST_OFFLINE) {
    snprintf(address, sizeof(address), "%s", st.current_ip[0] ? st.current_ip : info->internal);
  } else if (info->external[0] && strcmp(info->external, info->internal) != 0) {
    // The scan only looks nearby; connecting also tries the external address
    snprintf(address, sizeof(address), "%s", info->external);
  } else {
    vita_debug_log("[HOSTS] %s is offline: not asked to unpair", info->name);
    return false;
  }
  char key_dir[4096];
  snprintf(key_dir, sizeof(key_dir), "%s/%s", config.key_dir, info->name);
  int ret = gs_init(&server, address, info->port, key_dir, 0, true);
  if (ret != GS_OK) {
    vita_debug_log("[HOSTS] %s didn't answer at %s: %d", info->name, address, ret);
    return false;
  }
  // Asked even when it reports this Vita unpaired: over plain HTTP, which
  // gs_init falls back to, that report isn't reliable
  ret = gs_unpair(&server);
  vita_debug_log("[HOSTS] unpair request to %s: %d", info->name, ret);
  *told = ret == GS_OK;
  return pc_answered(ret);
}

host_unpair_result host_unpair(device_info_t *info) {
  bool told = false;
  bool answered = ask_pc_to_unpair(info, &told);
  if (!host_forget(info)) {
    return HOST_UNPAIR_FORGET_FAILED;
  }
  if (told) {
    return HOST_UNPAIR_DONE;
  }
  return answered ? HOST_UNPAIR_PC_REFUSED : HOST_UNPAIR_PC_UNREACHABLE;
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
