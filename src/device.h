#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct device_info device_info_t;
struct device_info {
  // The host's identity: its folder under key_dir (pairing keys, device.ini)
  // and the name used to find it by mDNS or DNS. Never changed by a rename.
  char name[256];
  // What the user calls it, if they renamed it; empty means use name
  char display_name[256];
  uint16_t port;
  bool paired;
  char internal[256];
  char external[256];
  char mac[18]; // XX:XX:XX:XX:XX:XX\0
  bool prefer_external;
  // The app last streamed from this PC through its menu, for the host
  // list's Continue row: its id and name, and when, as a count that grows
  // with each stream across all PCs (0: none yet). The Vita's clock can be
  // reset, so it isn't a time.
  int last_app_id;
  char last_app_name[256];
  unsigned int last_played;
};

typedef struct device_infos device_infos_t;
struct device_infos {
  int size;
  int count;
  device_info_t *devices;
};

extern device_infos_t known_devices;

device_info_t* find_device(const char *name);
// The name to show the user: the display name if one is set, otherwise name
const char *device_label(const device_info_t *info);
// The label of the known device at this address, or fallback if there isn't one
const char *device_label_for_address(const char *address, const char *fallback);
device_info_t* find_device_by_address(const char *address);
device_info_t* append_device(device_info_t *info);
bool update_device(device_info_t *info);
void load_all_known_devices();
bool load_device_info(device_info_t *info);
void save_device_info(const device_info_t *info);
bool remove_device(const char *name);
void device_file_path(char *out, const char *dir);
