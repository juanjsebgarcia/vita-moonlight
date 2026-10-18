// Returned by ui_settings_menu when the player asked to restart the paused
// stream with new settings (ui_connect_restart_stream)
#define SETTINGS_RESTART_STREAM 0x43

int ui_settings_menu();
void ui_settings_save_config();
// What a touchscreen corner's code does, in words (such as "Pause stream"),
// or the code itself when it isn't one of the listed keys. text holds 256.
void ui_settings_special_key_name(int ord, char *text);
