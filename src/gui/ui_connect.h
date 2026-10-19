#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../device.h"

void ui_connect_address(char *addr);
device_info_t* ui_connect_and_pairing(device_info_t *info);
bool ui_connect_connected();

void ui_connect_resume();
void ui_connect_manual();
// Asks for a PC's address, "host" or "host:port", starting from def, and asks
// again after saying what's wrong until it's valid. Returns 0 with host and
// port filled in, or -1 if the player cancels.
int ui_ask_address(const char *title, const char *def, char *host, size_t host_size, uint16_t *port);
void ui_connect_paired_device(device_info_t *info);

// Ends the paused stream and resumes the same app with the current stream
// settings, which the host applies on resume, then waits while it streams.
// For applying settings that only take effect on a new stream.
void ui_connect_restart_stream(void);
