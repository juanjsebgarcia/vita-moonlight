#include "../i18n/i18n.h"
#include "ui_settings.h"

#include "guilib.h"
#include "ime.h"

#include "../config.h"
#include "../device.h"
#include "../connection.h"
//#include "../debug.h"
#include "../input/vita.h"
#include "ui_connect.h"
#include "debug.h"
#include "udp_sniffer_vita.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

int end_search_thread(SceUID thid);
#include <vita2d.h>
#include <Limelight.h>
#include <client.h>
#include <errors.h>

#define MILLISECOND 1000
#define SECOND      (1000 * MILLISECOND)

enum {
  DEVICE_EXIT_SEARCH = 100,
  DEVICE_ITEM,
};

enum {
  DEVICE_VIEW_EXIT_SEARCH,
  DEVICE_VIEW_ITEM,
};

enum {
  SEARCH_THREAD_IDLE,
  SEARCH_THREAD_RUNNING,
  SEARCH_THREAD_REQ_STOP,
};

int search_thread_status = SEARCH_THREAD_IDLE;

static device_info_t devices[64];
static int DEVICE_ENTRY_IDX[64];
static int found_device = 0;

void ipv4_address_to_string(const struct sockaddr_in *addr, char *ip, const size_t len) {
  inet_ntop(AF_INET, &addr->sin_addr.s_addr, ip, len);
}

// part of publib. BSD license
// https://github.com/ajkaijanaho/publib/blob/master/strutil/strrstr.c
char* strrstr(const char *str, const char *pat) {
  size_t len, patlen;
  const char *p;

  assert(str != NULL);
  assert(pat != NULL);

  len = strlen(str);
  patlen = strlen(pat);

  if (patlen > len)
    return NULL;
  for (p = str + (len - patlen); p > str; --p)
    if (*p == *pat && strncmp(p, pat, patlen) == 0)
      return (char *) p;
  return NULL;
}

static void moonlight_found_callback(int idx, const char* host, const char* pcname, const char* ip, int port) {
    vita_debug_log("[mDNS] Dispositivo encontrado: %s (%s:%d)\n", pcname, ip, port);
    // Verificar si ya existe un dispositivo con el mismo nombre y misma IP
    for (int i = 0; i < found_device; i++) {
        if (strncmp(devices[i].name, pcname, sizeof(devices[i].name)) == 0 &&
            strncmp(devices[i].internal, ip, sizeof(devices[i].internal)) == 0) {
            vita_debug_log("[mDNS] Dispositivo duplicado ignorado: %s (%s)\n", pcname, ip);
            return;
        }
    }
    // Si el nombre es igual pero la IP es diferente, sí se agrega
    memset(&devices[found_device], 0, sizeof(device_info_t));
    strncpy(devices[found_device].name, pcname, sizeof(devices[found_device].name)-1);
    strncpy(devices[found_device].internal, ip, sizeof(devices[found_device].internal)-1);
    devices[found_device].port = port;
    found_device++;
}

int mdns_discovery_main(SceSize args, void *argp) {
  vita_debug_log("[mDNS] Iniciando búsqueda de dispositivos...\n");
  if (search_thread_status != SEARCH_THREAD_IDLE) {
    vita_debug_log("[mDNS] search_thread_status != IDLE, saliendo.\n");
    return 0;
  }

  search_thread_status = SEARCH_THREAD_RUNNING;
  found_device = 0;

  udp_sniffer_vita_deinit(); // Cierra socket y limpia estado
  udp_sniffer_vita_init();   // Limpia estado (opcional, por simetría)
  udp_sniffer_vita_set_callback(moonlight_found_callback);

  // Esperar y procesar durante 10 segundos máximo
  int ticks = 0;
  int max_ticks = 100; // 100 * 100ms = 10s
  while (search_thread_status == SEARCH_THREAD_RUNNING && ticks < max_ticks) {
      udp_sniffer_vita_poll();
      sceKernelDelayThread(1000 * 100); // 100ms
      ticks++;
  }

  search_thread_status = SEARCH_THREAD_IDLE;
  vita_debug_log("[mDNS] Búsqueda finalizada. found_device=%d\n", found_device);
  return 0;
}

static SceUID search_thread_id = -1;

void stop_search_thread_if_running() {
  vita_debug_log("[mDNS] stop_search_thread_if_running: status=%d, thid=%d\n", search_thread_status, search_thread_id);
  if (search_thread_status == SEARCH_THREAD_RUNNING && search_thread_id > 0) {
    vita_debug_log("[mDNS] Llamando a end_search_thread(%d)\n", search_thread_id);
    end_search_thread(search_thread_id);
    vita_debug_log("[mDNS] end_search_thread terminado\n");
    search_thread_id = -1;
  }
}

static void clear_devices() {
  vita_debug_log("[mDNS] Limpiando lista de dispositivos\n");
  memset(devices, 0, sizeof(devices));
  found_device = 0;
}

SceUID start_search_thread() {
  vita_debug_log("[mDNS] start_search_thread: limpiando y creando hilo\n");
  clear_devices();
  SceUID thid = sceKernelCreateThread("mdns", mdns_discovery_main, 0x10000100, 0x10000, 0, 0, NULL);
  if (thid < 0) {
    vita_debug_log("[mDNS] Error creando hilo de búsqueda\n");
    return -1;
  }
  sceKernelStartThread(thid, 0, 0);
  search_thread_id = thid;
  vita_debug_log("[mDNS] Hilo de búsqueda iniciado: thid=%d\n", thid);
  return thid;
}

// Prototipo adelantado para evitar warning
int end_search_thread(SceUID thid);

int end_search_thread(SceUID thid) {
  vita_debug_log("[mDNS] end_search_thread: solicitando parada de hilo %d\n", thid);
  search_thread_status = SEARCH_THREAD_REQ_STOP;

  SceUInt timeout;
  do {
    timeout = 100 * MILLISECOND;
  } while (sceKernelWaitThreadEnd(thid, NULL, &timeout) < 0);

  sceKernelDeleteThread(thid);
  vita_debug_log("[mDNS] Hilo %d eliminado\n", thid);
  return 0;
}

// The PC's address from the internet, for streaming away from home: the
// public address of this network, which the PC shares while the Vita is on
// its LAN. Looked up once after pairing a PC found by search, on a thread, so
// the menu doesn't wait for the STUN server (up to a few seconds). The main
// thread stores the result in external_lookup_poll().
enum {
  EXTERNAL_LOOKUP_IDLE,
  EXTERNAL_LOOKUP_RUNNING,
  EXTERNAL_LOOKUP_DONE,
};

static volatile int external_lookup_status = EXTERNAL_LOOKUP_IDLE;
static char external_lookup_name[256];
// The PC's external address when the lookup began; a change since (Change IP)
// wins over the lookup
static char external_lookup_before[256];
static char external_lookup_address[16];

static int external_lookup_main(SceSize args, void *argp) {
  unsigned int extern_addr = 0;
  external_lookup_address[0] = '\0';
  if (LiFindExternalAddressIP4("stun.moonlight-stream.org", 3478, &extern_addr) == 0) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = extern_addr;
    ipv4_address_to_string(&addr, external_lookup_address, sizeof(external_lookup_address));
  }
  __sync_synchronize(); // the address is written before the status says so
  external_lookup_status = EXTERNAL_LOOKUP_DONE;
  return sceKernelExitDeleteThread(0);
}

static void external_lookup_start(const device_info_t *info) {
  const char *name = info->name;
  // One at a time; a PC paired meanwhile keeps the address it has
  if (external_lookup_status != EXTERNAL_LOOKUP_IDLE) {
    vita_debug_log("[STUN] Lookup already running, skipped for %s\n", name);
    return;
  }
  snprintf(external_lookup_name, sizeof(external_lookup_name), "%s", name);
  // As the device list has it, which is what external_lookup_poll() compares
  const device_info_t *known = find_device(name);
  snprintf(external_lookup_before, sizeof(external_lookup_before), "%s", (known ? known : info)->external);
  external_lookup_status = EXTERNAL_LOOKUP_RUNNING;
  SceUID thid = sceKernelCreateThread("stun", external_lookup_main, 0x10000100, 0x10000, 0, 0, NULL);
  if (thid < 0 || sceKernelStartThread(thid, 0, NULL) < 0) {
    vita_debug_log("[STUN] Cannot start the lookup thread\n");
    if (thid >= 0) {
      sceKernelDeleteThread(thid);
    }
    external_lookup_status = EXTERNAL_LOOKUP_IDLE;
  }
}

void external_lookup_poll() {
  if (external_lookup_status != EXTERNAL_LOOKUP_DONE) {
    return;
  }
  __sync_synchronize(); // pairs with the thread's, so the address is complete
  // The device list belongs to the main thread, so look the PC up again here
  device_info_t *info = find_device(external_lookup_name);
  vita_debug_log("[STUN] %s: external address %s\n", external_lookup_name,
                 external_lookup_address[0] ? external_lookup_address : "not found");
  if (info != NULL && external_lookup_address[0] && strcmp(info->external, external_lookup_before) == 0) {
    snprintf(info->external, sizeof(info->external), "%s", external_lookup_address);
    save_device_info(info);
  }
  external_lookup_status = EXTERNAL_LOOKUP_IDLE;
}

static int ui_search_device_callback(int id, void *context, const input_data *input) {
  if (ui_pressed(input, UI_INTENT_HELP)) {
    vita_debug_log("[mDNS] TRIÁNGULO presionado: refrescando búsqueda\n");
    stop_search_thread_if_running();
    start_search_thread();
    return 2; // Fuerza refresco del menú
  }
  if (!ui_pressed(input, UI_INTENT_CONFIRM)) {
    // if remain slot, reload discovered devices
    if (!DEVICE_ENTRY_IDX[DEVICE_VIEW_ITEM + found_device - 1]) {
      return 2;
    }
    return 0;
  }
  if (id == DEVICE_EXIT_SEARCH) {
    return 1;
  }
  if (id >= DEVICE_ITEM) {

    // Usar IP y puerto detectados por mDNS
    device_info_t *dev = &devices[id - DEVICE_ITEM];
    strncpy(dev->internal, dev->internal, sizeof(dev->internal)-1); // IP ya está
    dev->internal[sizeof(dev->internal)-1] = '\0';
    dev->port = dev->port; // Puerto ya está
    // La MAC ya está en dev->mac si mDNS la proveyó
    device_info_t *info = ui_connect_and_pairing(dev);
    if (info == NULL) {
      return 0;
    }

    save_device_info(info);
    external_lookup_start(info);

    return 1;
  }
  //TODO: Return proper on all code paths
  return 0;
}

static int ui_search_device_back(void *context) {
  return 0;
}

int ui_search_device_loop() {
  int idx = 0;
  menu_entry menu[64];
  memset(DEVICE_ENTRY_IDX, 0, sizeof(DEVICE_ENTRY_IDX));

#define MENU_CATEGORY(NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .disabled = true, .separator = true, .kind = MENU_ROW_HEADER }; \
    idx++; \
  } while (0)
#define MENU_ENTRY(ID, TAG, NAME, SUFFIX) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .id = (ID), .suffix = (SUFFIX) }; \
    DEVICE_ENTRY_IDX[(TAG)] = idx; \
    idx++; \
  } while(0)
#define MENU_MESSAGE(MESSAGE) \
  do { \
    menu[idx] = (menu_entry) { .name = "", .disabled = true }; \
    snprintf(menu[idx].subname, sizeof(menu[idx].subname), "%s", (MESSAGE)); \
    idx++; \
  } while(0)
#define MENU_SEPARATOR() \
  do { \
    menu[idx] = (menu_entry) { .name = "", .disabled = true, .separator = true }; \
    idx++; \
  } while(0)

  // Mostrar mensaje de refresco con triángulo
  MENU_MESSAGE((char *)tr(STR_SEARCH_REFRESH_HINT));

  MENU_CATEGORY((char *)tr(STR_SEARCH_TITLE));
  for (int i = 0; i < found_device; i++) {
    if (devices[i].internal[0] == '\0') {
      continue;
    }
    device_info_t *p = find_device(devices[i].name);
    if (p && p->paired) {
      continue;
    }
    // Mostrar MAC en el sufijo si está disponible
    char suffix[64] = {0};
    if (devices[i].mac[0]) {
      snprintf(suffix, sizeof(suffix), "%s [%s]", devices[i].internal, devices[i].mac);
    } else {
      snprintf(suffix, sizeof(suffix), "%s", devices[i].internal);
    }
    MENU_ENTRY(DEVICE_ITEM + i, DEVICE_VIEW_ITEM + i, devices[i].name, suffix);
  }
  MENU_SEPARATOR();
  MENU_ENTRY(DEVICE_EXIT_SEARCH, DEVICE_VIEW_EXIT_SEARCH, (char *)tr(STR_SEARCH_BACK), "");

  search_view search = {.searching = search_thread_status == SEARCH_THREAD_RUNNING};
  menu_screen screen = {
    .id = UI_SCREEN_SEARCH,
    .title = tr(STR_SEARCH_TITLE),
    .search = &search,
  };
  return display_menu(menu, idx, &screen, &ui_search_device_callback, &ui_search_device_back, &menu);
}

void ui_search_device() {
  stop_search_thread_if_running(); // Siempre detener cualquier búsqueda previa
  start_search_thread(); // Siempre iniciar búsqueda al entrar

  while (ui_search_device_loop() == 2);

  stop_search_thread_if_running(); // Detener búsqueda al salir
}
