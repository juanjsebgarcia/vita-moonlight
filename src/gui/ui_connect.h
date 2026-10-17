#include <stdbool.h>
#include <stddef.h>
#include "../device.h"

void ui_connect_address(char *addr);
device_info_t* ui_connect_and_pairing(device_info_t *info);
bool ui_connect_connected();

void ui_connect_resume();
void ui_connect_manual();
void ui_connect_paired_device(device_info_t *info);

// Connects to a paired host and reads its app list, without any menus or
// dialogs. Returns 0, or a negative code with a message in err.
int ui_connect_open_headless(device_info_t *info, char *err, size_t err_len);
// True when connected to a host (its app list loaded) and nothing is streaming
bool ui_connect_server_ready(void);
// The connected host's name, or an empty string
const char *ui_connect_server_name(void);
// Finds the benchmark app on the connected host: an exact, case-insensitive
// match on preferred, otherwise the first app with "bench" in its name.
// Returns its id with its name in found, or -1 if there isn't one.
int ui_connect_find_bench_app(const char *preferred, char *found, size_t found_len);
// Finds an app on the connected host by name, ignoring case. Returns its id,
// or -1 if there isn't one.
int ui_connect_find_app(const char *name);
// Starts streaming an app on the connected host. Returns 0, or a negative
// code with a message in err.
int ui_connect_start_app_by_id(int app_id, char *err, size_t err_len);
// After a stream started without the host menu has ended, restores the
// connection state that menu expects
void ui_connect_return_to_server(void);
// Quits the app running on the host the last connection used
int ui_connect_quit_app(void);
