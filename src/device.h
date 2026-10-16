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
device_info_t* find_device_by_address(const char *address);
device_info_t* append_device(device_info_t *info);
bool update_device(device_info_t *info);
void load_all_known_devices();
bool load_device_info(device_info_t *info);
void save_device_info(const device_info_t *info);
bool remove_device(const char *name);
void device_file_path(char *out, const char *dir);
