#include <stdbool.h>
#include <stddef.h>
#include "../device.h"

void ui_connect_address(char *addr);
device_info_t* ui_connect_and_pairing(device_info_t *info);
bool ui_connect_connected();

void ui_connect_resume();
void ui_connect_manual();
void ui_connect_paired_device(device_info_t *info);
// Connects to a paired host and starts the app it last streamed, or resumes
// it if it's still running there, then shows the PC's menu as streaming from
// it would
void ui_connect_continue(device_info_t *info);

// Ends the paused stream and resumes the same app with the current stream
// settings, which the host applies on resume, then waits while it streams.
// For applying settings that only take effect on a new stream.
void ui_connect_restart_stream(void);
