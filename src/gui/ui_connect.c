#include "ui_connect.h"

#include "Limelight.h"
#include "errors.h"
#include "guilib.h"
#include "ime.h"
#include "ui_boxart.h"
#include "ui_settings.h"

#include "../connection.h"
#include "../configuration.h"
#include "../video.h"
#include "../config.h"
#include "../util.h"
#include "../device.h"
#include "../debug.h"
#include "../netcount.h"

#include "client.h"
#include "errors.h"
#include "../platform.h"

#include "../power/vita.h"
#include "../input/vita.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <vita2d.h>

SERVER_DATA server;
PAPP_LIST server_applist;
// Name of the host the current connection is to, as it was paired
static char current_host[256] = "";
int pos[2];

int get_app_id(PAPP_LIST list, char *name) {
  while (list != NULL) {
    if (strcmp(list->name, name) == 0)
      return list->id;

    list = list->next;
  }
  return -1;
}

int get_app_name(PAPP_LIST list, int id, char *name) {
  while (list != NULL) {
    if (list->id == id) {
      strcpy(name, list->name);
      return 1;
    }

    list = list->next;
  }

  return 0;
}

// Set by start_stream once the host has launched the app, even if the stream
// then fails to start
static bool start_launched_app = false;

// Starts streaming appId. Returns 0, or a negative code with a message in err,
// so callers decide whether to show it (the benchmark runs without dialogs).
static int start_stream(int appId, char *err, size_t err_len) {
  start_launched_app = false;
  // TODO support force controller id
  vita_debug_log("Running gs_start_app...");
  int ret = gs_start_app(&server, &config.stream, appId, config.sops, config.localaudio, 1);
  if (ret < 0) {
    if (ret == GS_NOT_SUPPORTED_4K)
      snprintf(err, err_len, "Server doesn't support 4K");
    else if (ret == GS_NOT_SUPPORTED_MODE)
      snprintf(err, err_len, "Server doesn't support %dx%d (%d fps)", config.stream.width, config.stream.height, config.stream.fps);
    else
      snprintf(err, err_len, "Errorcode starting app: %d", ret);

    return ret;
  }
  vita_debug_log("App started.");
  start_launched_app = true;

  enum platform system = VITA;
  int drFlags = 0;

  if (config.fullscreen)
    drFlags |= DISPLAY_FULLSCREEN;

  DECODER_RENDERER_CALLBACKS *video_callback = platform_get_video(system);
  if (config.direct_decode) {
    video_callback->capabilities |= CAPABILITY_DIRECT_SUBMIT;
  } else {
    video_callback->capabilities &= ~CAPABILITY_DIRECT_SUBMIT;
  }
  if (config.enable_ref_frame_invalidation) {
    video_callback->capabilities |= CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC;
  } else {
    video_callback->capabilities &= ~CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC;
  }

  // --- Ajuste para soporte Host Resolution: no modificar resolución si es -1 ---
  // Eliminados g_requested_width y g_requested_height, lógica simplificada
  int orig_width = config.stream.width;
  int orig_height = config.stream.height;
  // Si se seleccionó una resolución específica, se mantiene; si es -1, se deja al host
  // (No se modifica config.stream.width/height aquí)

  vita_debug_log("Running LiStartConnection...");
  ret = LiStartConnection(&server.serverInfo, &config.stream, &connection_callbacks,
                          video_callback, platform_get_audio(system),
                          NULL, drFlags, NULL, 0);
  vita_debug_log("Connection started.");

  // Restaurar resolución real para el framebuffer/render
  config.stream.width = orig_width;
  config.stream.height = orig_height;

  if (ret == 0) {
    server.currentGame = appId;
    return 0;
  }

  const char* connection_failed_stage_name = LiGetStageName(connection_stage);
  snprintf(err, err_len, "Failed to start stream:\nFailed stage: %s\n(error code %d)",
           connection_failed_stage_name, ret);
  return ret < 0 ? ret : -1;
}

void ui_connect_stream(int appId) {
  char err[512];
  if (start_stream(appId, err, sizeof(err)) != 0) {
    display_error("%s", err);
  }
}

int ui_connect_open_headless(device_info_t *info, char *err, size_t err_len) {
  char key_dir[4096];
  snprintf(key_dir, sizeof(key_dir), "%s/%s", config.key_dir, info->name);

  // Try the address that worked last time first
  const char *addresses[2] = {
    info->prefer_external ? info->external : info->internal,
    info->prefer_external ? info->internal : info->external,
  };
  int ret = GS_ERROR;
  for (int i = 0; i < 2 && ret != GS_OK; i++) {
    if (addresses[i][0] == '\0') {
      continue;
    }
    // Same as the interactive path: skip the host display-mode check, which
    // Sunshine's mode list doesn't reliably pass (e.g. 960x540 at 50 fps)
    ret = gs_init(&server, (char *)addresses[i], info->port, key_dir, 0, true);
  }
  if (ret != GS_OK) {
    snprintf(err, err_len, "Can't connect to %s: %s", info->name, gs_error ? gs_error : "no response");
    return -1;
  }
  snprintf(current_host, sizeof(current_host), "%s", info->name);
  connection_reset();

  // Forget the previous PC's apps first: if this PC's list can't be read,
  // a stale list would let a later attempt start one of those IDs here.
  // Not freed, as elsewhere: menu rows may still point at its names.
  server_applist = NULL;
  if (gs_applist(&server, &server_applist) != GS_OK) {
    snprintf(err, err_len, "Can't read the app list from %s: %s", info->name, gs_error ? gs_error : "");
    return -1;
  }
  connection_paired();
  return 0;
}

bool ui_connect_server_ready(void) {
  int status = connection_get_status();
  return server.paired && server_applist != NULL && (status == LI_READY || status == LI_PAIRED);
}

const char *ui_connect_server_name(void) {
  return current_host;
}

static bool contains_ignoring_case(const char *haystack, const char *needle) {
  size_t n = strlen(needle);
  for (; *haystack; haystack++) {
    if (strncasecmp(haystack, needle, n) == 0) {
      return true;
    }
  }
  return false;
}

int ui_connect_find_bench_app(const char *preferred, char *found, size_t found_len) {
  PAPP_LIST match = NULL;
  for (PAPP_LIST app = server_applist; app != NULL && match == NULL; app = app->next) {
    if (preferred && preferred[0] && strcasecmp(app->name, preferred) == 0) {
      match = app;
    }
  }
  for (PAPP_LIST app = server_applist; app != NULL && match == NULL; app = app->next) {
    if (contains_ignoring_case(app->name, "bench")) {
      match = app;
    }
  }
  if (match == NULL) {
    return -1;
  }
  snprintf(found, found_len, "%s", match->name);
  return match->id;
}

int ui_connect_find_app(const char *name) {
  for (PAPP_LIST app = server_applist; app != NULL; app = app->next) {
    if (strcasecmp(app->name, name) == 0) {
      return app->id;
    }
  }
  return -1;
}

int ui_connect_start_app_by_id(int app_id, char *err, size_t err_len) {
  // Auto-detect and the benchmark quit their app afterwards. Over a running
  // app, the host would resume that one instead, and they'd then close it.
  if (server.currentGame != 0) {
    snprintf(err, err_len, "An app is already running on %s. Quit it from the "
             "PC's menu first.", ui_connect_server_name());
    return -1;
  }
  vitapower_config(config);
  vitainput_config(config);
  connection_paired();
  // Count this stream's video packets afresh (src/netcount.c)
  netcount_reset();
  int ret = start_stream(app_id, err, err_len);
  if (ret != 0 && start_launched_app) {
    // The app started but its stream didn't: close it again, since the host
    // was idle before (this also clears server.currentGame)
    ui_connect_quit_app();
    ui_connect_return_to_server();
  }
  return ret;
}

void ui_connect_return_to_server(void) {
  // A finished stream leaves the connection disconnected; go back to the
  // state the host's menu expects
  if (connection_get_status() == LI_DISCONNECTED) {
    connection_reset();
  }
  connection_paired();
}

int ui_connect_quit_app(void) {
  int ret = gs_quit_app(&server);
  if (ret == GS_OK) {
    // As the menu's Quit does, so the apps can be started again
    server.currentGame = 0;
  }
  return ret;
}

enum {
  CONNECT_PAIRUNPAIR = 13,
  CONNECT_DISCONNECT,
  CONNECT_QUITAPP,
  CONNECT_SETTINGS,
  // The row of box art covers, which stands for every app
  CONNECT_BOXART
};

#define QUIT_RELOAD 2

int ui_connect_loop(int id, void *context, const input_data *input) {
  int status = connection_get_status();

  if (status == LI_DISCONNECTED) {
      goto disconnect;
  }

  menu_entry *menu = context;
  for (int i = pos[0]; i < pos[1]; i += 1) {
    menu[i].disabled = (server.currentGame != 0);
  }

  ui_boxart_set_focus(id == CONNECT_BOXART);
  if (id == CONNECT_BOXART) {
    ui_boxart_input(input->buttons);
  }

  if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
    return 0;
  }

  if (id == CONNECT_BOXART) {
    // Starts the selected cover's app, as choosing it in the text list does
    id = ui_boxart_selected_app();
    if (id == 0) {
      return 0;
    }
  }

  int ret;

  switch (id) {
    case CONNECT_SETTINGS: {
      int app_list_style = config.app_list_style;
      // Settings that apply to a running stream take effect on Resume, which
      // reloads the input and power settings and restarts output. The rest
      // need a new stream, which Settings can ask for.
      if (ui_settings_menu() == SETTINGS_RESTART_STREAM) {
        ui_connect_restart_stream();
        return connection_get_status() == LI_DISCONNECTED ? 1 : QUIT_RELOAD;
      }
      // Rebuild to show the apps in the newly chosen style
      return config.app_list_style != app_list_style ? QUIT_RELOAD : 0;
    }
    case CONNECT_PAIRUNPAIR:
      if (server.paired) {
        flash_message("Unpairing...");
        ret = gs_unpair(&server);
        if (ret == GS_OK) {
          if (connection_terminate()) {
            display_error("Reconnect failed: %d", -1);
            return 0;
          }
          return QUIT_RELOAD;
        }
        display_error("Unpairing failed: %d", ret);
        return 0;
      }

      char pin[5];
      sprintf(pin, "%d%d%d%d",
              (uint32_t)rand() % 10, (uint32_t)rand() % 10, (uint32_t)rand() % 10, (uint32_t)rand() % 10);
      flash_message("Please enter the following PIN\non the target PC:\n\n%s", pin);
      vita_debug_log("[UI] gs_pair: calling gs_pair at %u", (unsigned)time(NULL));
      ret = gs_pair(&server, &pin[0]);
      vita_debug_log("[UI] gs_pair: returned ret=%d at %u", ret, (unsigned)time(NULL));
      if (ret == 0) {
        connection_paired();
        // After pairing, save server MAC into known device if present
        device_info_t *dev = find_device_by_address(server.serverInfo.address);
        if (dev) {
          dev->paired = true;
          char mac[18] = {0};
          if (gs_get_server_mac(&server, mac, sizeof(mac)) == GS_OK && mac[0]) {
            strncpy(dev->mac, mac, 17);
            dev->mac[17] = '\0';
            vita_debug_log("[PAIR] MAC obtenida y guardada en device.ini: %s for device %s", dev->mac, dev->name);
          } else {
            vita_debug_log("[PAIR] No se encontró MAC en serverinfo para %s", server.serverInfo.address);
          }
          save_device_info(dev);
          vita_debug_log("[PAIR] After save_device_info(dev) - time: %u", (unsigned)time(NULL));
          // Notify user pairing succeeded: show a short message so the PIN dialog
          // (which was drawn earlier) is replaced by a success message.
          flash_message("Paired: %s", dev->name);
        } else {
          vita_debug_log("[PAIR] Dispositivo no encontrado para la IP %s al guardar MAC", server.serverInfo.address);
        }
        if (connection_terminate()) {
          display_error("Reconnect failed: %d", -2);
          return 0;
        }
        return QUIT_RELOAD;
      }
      display_error("Pairing failed: %d\n Error: %s", ret, gs_error);
      return 0;

    case CONNECT_DISCONNECT:
      goto disconnect;

    case CONNECT_QUITAPP:
      flash_message("Quitting...");
      ret = gs_quit_app(&server);
      if (ret == GS_OK) {
        connection_paired();
        server.currentGame = 0;
        return QUIT_RELOAD;
      }
      display_error("Quitting failed: %d", ret);
      return 0;

    // application launcher / resume
    default:
      vitapower_config(config);
      vitainput_config(config);

      switch (status) {
        case LI_MINIMIZED:
          if (server.currentGame == id) {
            // The covers drawn this frame stay in memory until the menu closes
            ui_boxart_stop_loading();
            sceKernelDelayThread(500 * 1000);
            connection_resume();
            break;
          }
          // TODO: stop previous stream
        case LI_PAIRED:
          flash_message("Stream starting...");
          // That frame has finished drawing, so the covers can go now: the
          // stream needs the memory and the network more
          ui_boxart_close();
          ui_connect_stream(id);
          break;
      }

//mainloop:
      while (connection_is_connected()) {
        sceKernelDelayThread(500 * 1000);
      }

      int status = connection_get_status();

      if (status == LI_DISCONNECTED) {
          goto disconnect;
      }

      return QUIT_RELOAD;
  }

disconnect:
  flash_message("Disconnecting...");
  connection_terminate();
  sceKernelDelayThread(1000 * 1000);
  return 1;
}

int ui_connect(char *name, char *address, uint16_t port) {
  int ret;
  if (!connection_is_ready()) {
    flash_message("Connecting to:\n %s:%d...", address, port);

    char key_dir[4096];
    sprintf(key_dir, "%s/%s", config.key_dir, name);

    ret = gs_init(&server, address, port, key_dir, 0, true);
    snprintf(current_host, sizeof(current_host), "%s", name);
    if (ret == GS_OUT_OF_MEMORY) {
      display_error("Not enough memory");
      return 0;
    } else if (ret == GS_INVALID) {
      display_error("Invalid data received from server: %s\n", address, gs_error);
      return 0;
    } else if (ret == GS_UNSUPPORTED_VERSION) {
      if (!config.unsupported_version) {
        display_error("Unsupported version: %s\n", gs_error);
        return 0;
      }
    } else if (ret == GS_ERROR) {
      display_error("Gamestream error: %s\n", gs_error);
      return 0;
    } else if (ret != GS_OK) {
      display_error("Can't connect to server\n%s", address);
      return 0;
    }

    connection_reset();
  }
  return 1;
}

int ui_connected_menu() {
  int ret;
  int app_count = 0;
  // pos[] marks the app rows, which ui_connect_loop disables mid-stream; clear
  // it so a menu without them never uses the last menu's rows
  pos[0] = pos[1] = 0;
  if (server.paired) {
    ret = gs_applist(&server, &server_applist);
    if (ret != GS_OK) {
      display_error("Can't get applist!\n%d\n%s", ret, gs_error);
      return 0;
    }

    if (server_applist != NULL) {
      sort_app_list(server_applist);
      PAPP_LIST list = server_applist;
      while (list) {
        list = list->next;
        app_count += 1;
      }
    }
  }

  // current menu = 11 + app_count. but little more alloc ;)
  struct menu_entry menu[app_count + 16];

  int idx = 0;

#define MENU_CATEGORY(NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .disabled = true, .separator = true }; \
    idx++; \
  } while (0)
#define MENU_ENTRY(ID, NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .id = (ID) }; \
    idx++; \
  } while(0)
#define MENU_MESSAGE(MESSAGE, COLOR) \
  do { \
    menu[idx] = (menu_entry) { .name = (MESSAGE), .disabled = true, .color = (COLOR) }; \
    idx++; \
  } while(0)

  //header
  //MENU
  MENU_MESSAGE("Connected to the server:", 0xffffffff);
  char server_info[256];
  unsigned short port = server.httpPort ? server.httpPort : server.httpsPort;
  snprintf(server_info, sizeof(server_info),
           "IP: %s  PORT: %d, GPU %s, GFE %s",
           server.serverInfo.address,
           port,
           server.gpuType,
           server.serverInfo.serverInfoGfeVersion);

  MENU_MESSAGE(server_info, 0xffffffff);

  // Covers need the room under the menu, so the menu is kept short
  bool boxart = config.app_list_style == APP_LIST_BOXART && server.paired && server_applist != NULL;
  if (!boxart) {
    MENU_MESSAGE("", 0);
  }

  if (!server.paired) {
    // pairing
    MENU_CATEGORY("Not paired");
    MENU_ENTRY(CONNECT_PAIRUNPAIR, "Pair");

    MENU_ENTRY(CONNECT_DISCONNECT, "Disconnect");
  } else {
    // current stream
    if (server.currentGame != 0) {
      char current_appname[256];
      char current_status[256];

      if (!get_app_name(server_applist, server.currentGame, current_appname)) {
        strcpy(current_appname, "unknown");
      }
      sprintf(current_status, "Streaming %s", current_appname);

      MENU_CATEGORY(current_status);
      MENU_ENTRY(server.currentGame, "Resume");
      MENU_ENTRY(CONNECT_SETTINGS, "Settings");
      MENU_ENTRY(CONNECT_QUITAPP, "Quit");
    }

    // pairing
    MENU_CATEGORY("Paired");
    connection_paired();
    // FIXME: unpair not work
    // MENU_ENTRY(CONNECT_PAIRUNPAIR, "Unpair");

    // While streaming, Settings is listed with Resume and Quit instead
    if (server.currentGame == 0) {
      MENU_ENTRY(CONNECT_SETTINGS, "Settings");
    }
    MENU_ENTRY(CONNECT_DISCONNECT, "Disconnect");

    // app list
    if (boxart) {
      pos[0] = idx;
      MENU_ENTRY(CONNECT_BOXART, "Applications");
      menu[idx - 1].suffix = ui_boxart_counter();
      pos[1] = idx;
    } else if (server_applist != NULL) {
      MENU_CATEGORY("Applications");

      pos[0] = idx;

      PAPP_LIST list = server_applist;
      while (list) {
        MENU_ENTRY(list->id, list->name);
        list = list->next;
      }

      pos[1] = idx;
    }
  }

  if (!boxart) {
    return display_menu(menu, idx, NULL, &ui_connect_loop, NULL, NULL, &menu);
  }

  // The menu sits at the top, as tall as its rows, with the covers beneath it
  menu_geom geom = make_geom_centered(600, (idx + 1) * 24);
  geom.y = 30;
  geom.total_y = geom.y + geom.height;
  ui_boxart_open(&server, current_host, server_applist, server.currentGame);
  ret = display_menu(menu, idx, &geom, &ui_connect_loop, NULL, &ui_boxart_draw, &menu);
  // display_menu has finished its last frame, so the textures can be freed
  ui_boxart_close();
  return ret;
}

device_info_t* ui_connect_and_pairing(device_info_t *info) {
  flash_message("Test connecting to:\n %s...", info->internal);
  vita_debug_log("[CONNECT] Intentando conectar: name=%s, ip=%s, port=%d", info->name, info->internal, info->port);
  char key_dir[4096];
  sprintf(key_dir, "%s/%s", config.key_dir, info->name);
  sceIoMkdir(key_dir, 0777);

  int ret = gs_init(&server, info->internal, info->port, key_dir, 0, true);

  if (ret == GS_OUT_OF_MEMORY) {
    display_error("Not enough memory");
    return NULL;
  } else if (ret == GS_INVALID) {
    display_error("Invalid data received from server: %s\n", info->internal, gs_error);
    return NULL;
  } else if (ret == GS_UNSUPPORTED_VERSION) {
    if (!config.unsupported_version) {
      display_error("Unsupported version: %s\n", gs_error);
      return NULL;
    }
  } else if (ret == GS_ERROR) {
    display_error("Gamestream error: %s\n", gs_error);
    return NULL;
  } else if (ret != GS_OK) {
    display_error("Can't connect to server\n%s", info->internal);
    return NULL;
  }

  connection_reset();

  device_info_t *p = append_device(info);
  if (p == NULL) {
    ret = update_device(info);
    if (ret == false) {
      display_error("Could not update device info");
    }

  } else {
    info = p;
  }

  // connectable address
  save_device_info(info);
  vita_debug_log("[PAIR] After save_device_info(info) - time: %u", (unsigned)time(NULL));
  // Notify user pairing succeeded
  flash_message("Paired: %s", info->name);

  if (server.paired) {
    // no more need, move next action
    goto paired;
  }

  char pin[5];
  sprintf(pin, "%d%d%d%d",
          (int)rand() % 10, (int)rand() % 10, (int)rand() % 10, (int)rand() % 10);
  flash_message("Please enter the following PIN\non the target PC:\n\n%s", pin);

  ret = gs_pair(&server, pin);
  if (ret != GS_OK) {
    display_error("Pairing failed: %d", ret);
    connection_terminate();
    return NULL;
  }

paired:
  connection_paired();

  info->paired = true;

  // Preferimos usar la MAC ya obtenida en serverinfo (XML) si está disponible
  char mac[18] = {0};
  if (gs_get_server_mac(&server, mac, sizeof(mac)) == GS_OK && mac[0]) {
    strncpy(info->mac, mac, 17);
    info->mac[17] = '\0';
    vita_debug_log("[PAIR] MAC obtenida tras pairing (serverinfo): %s", info->mac);
  } else {
    vita_debug_log("[PAIR] No se encontró MAC en serverinfo; no se guarda MAC automáticamente");
  }
  save_device_info(info);

  if (connection_terminate()) {
    display_error("Reconnect failed: %d", -2);
    return info;
  }

  return info;
}

void ui_connect_restart_stream(void) {
  int app_id = server.currentGame;
  if (app_id == 0 || connection_get_status() != LI_MINIMIZED) {
    return;
  }
  flash_message("Restarting the stream with the new settings...");
  // The app keeps running on the host; only our stream ends
  connection_terminate();
  sceKernelDelayThread(500 * 1000);
  connection_reset();
  connection_paired();
  vitapower_config(config);
  vitainput_config(config);
  // A resume carries the new resolution, frame rate and bitrate
  ui_connect_stream(app_id);
  while (connection_is_connected()) {
    sceKernelDelayThread(500 * 1000);
  }
}

void ui_connect_resume() {
  while (ui_connected_menu() == QUIT_RELOAD);
}

void ui_connect_manual() {
  device_info_t info = {0};
  if (ime_dialog_string(info.name, "Enter Name:", "") != 0) {
    return;
  }
  if (ime_dialog_string(info.internal, "Enter IP or Address:", "") != 0) {
    return;
  }
  info.port = 47989;
  ui_connect_and_pairing(&info);
}

bool check_connection(const char *name, char *addr, uint16_t port) {
  // someone already connected
  if (connection_is_ready()) {
    return false;
  }

  flash_message("Check connecting to:\n %s:%d...", addr, port);

  int log_level = 0;
  if (config.save_debug_log) {
    log_level = 3;
  }

  char key_dir[4096];
  sprintf(key_dir, "%s/%s", config.key_dir, name);

  if (gs_init(&server, addr, port, key_dir, log_level, true) != GS_OK) {
    return false;
  }

  vita_debug_log("Connection check succeded.");

  connection_terminate();
  return true;
}

void ui_connect_paired_device(device_info_t *info) {
  if (!info->paired) {
    display_error("Unpaired device\n%s", info->name);
    return;
  }

  char *addr = NULL;
  if (info->prefer_external && check_connection(info->name, info->external, info->port)) {
    addr = info->external;
  } else if (check_connection(info->name, info->internal, info->port)) {
    addr = info->internal;
  } else if (check_connection(info->name, info->external, info->port)) {
    addr = info->external;
  }
  info->prefer_external = addr == info->external;
  save_device_info(info);

  if (addr == NULL) {
    display_error("Can't connect to server\n%s", info->name);
    return;
  }

  if (!ui_connect(info->name, addr, info->port)) {
    return;
  }

  while (ui_connected_menu() == QUIT_RELOAD);
}

bool ui_connect_connected() {
  return connection_is_ready();
}

void ui_connect_address(char *addr) {
  strcpy(addr, server.serverInfo.address);
}
