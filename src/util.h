
#include "../libgamestream/xml.h"
#include "psp2/net/net.h"
#include <stdbool.h>

void sort_app_list(PAPP_LIST list);
int ensure_buf_size(void **buf, size_t *buf_size, size_t required_size);
void cycle_vita_sockets();
// True when Bluetooth is switched on in the Vita's settings
bool vita_bluetooth_enabled(void);
// The Wi-Fi signal strength in percent, or -1 when not connected
int vita_wifi_percent(void);
enum {
  VITA_NETWORK_NONE,
  VITA_NETWORK_WIFI,
  VITA_NETWORK_WIRED,  // a PS TV's Ethernet port
  VITA_NETWORK_MOBILE, // a 3G Vita's modem
};
// Which kind of network the Vita is on, one of VITA_NETWORK_*
int vita_network_type(void);
int load_or_refresh_vita_network(SceNetInitParam *net_param);