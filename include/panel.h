#ifndef LEMONBAR_C_PANEL_H
#define LEMONBAR_C_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PANEL_TEXT_MAX 4096
#define PANEL_PATH_MAX 4096
#define PANEL_ARG_MAX 16

typedef struct {
    char font[128], icon_font[128], geometry[64], wm_name[64];
    char terminal[64], location[128], language[16];
    char launcher[PANEL_PATH_MAX], power_menu[PANEL_PATH_MAX];
    char weather_cache[PANEL_PATH_MAX], weather_image[PANEL_PATH_MAX];
    char color_panel_bg[16], color_bg[16], color_fg[16], color_free[16], color_focus[16];
    char color_free_bg[16], color_focused_free[16], color_focused_free_bg[16];
    char color_occupied[16], color_occupied_bg[16];
    char color_focused_occupied[16], color_focused_occupied_bg[16];
    char color_urgent[16], color_urgent_bg[16];
    char color_focused_urgent[16], color_focused_urgent_bg[16];
    char color_clock[16], color_volume[16], color_muted[16], color_system[16];
    char color_network[16], color_weather[16], color_battery[16];
    char color_warning[16], color_critical[16], color_brightness[16];
    int height, clickable_areas, underline, volume_step, brightness_step;
    unsigned weather_interval, network_interval, title_max;
} panel_config;

typedef struct {
    uint64_t cpu_total, cpu_idle;
    bool cpu_initialized;
    char workspace[PANEL_TEXT_MAX], title[PANEL_TEXT_MAX];
    char launcher[PANEL_TEXT_MAX], weather[PANEL_TEXT_MAX], battery[PANEL_TEXT_MAX];
    char network[PANEL_TEXT_MAX], brightness[PANEL_TEXT_MAX], volume[PANEL_TEXT_MAX];
    char cpu[PANEL_TEXT_MAX], clock[PANEL_TEXT_MAX], tray[PANEL_TEXT_MAX];
    char power[PANEL_TEXT_MAX], screencast[PANEL_TEXT_MAX];
} panel_state;

void config_defaults(panel_config *cfg);
int config_load(panel_config *cfg, const char *path, char *error, size_t error_size);
int mkdir_p(const char *path, mode_t mode);
int read_text_file(const char *path, char *buffer, size_t size);
int write_atomic(const char *path, const char *data, mode_t mode);
bool command_exists(const char *name);
int run_capture(char *const argv[], char *output, size_t size, int timeout_ms);
int spawn_detached(char *const argv[]);
void shell_quote_action(const char *input, char *output, size_t size);
void log_message(const char *level, const char *format, ...);

void module_clock(const panel_config *cfg, panel_state *state);
void module_cpu(const panel_config *cfg, panel_state *state);
void module_battery(const panel_config *cfg, panel_state *state);
void module_screencast(const panel_config *cfg, panel_state *state, const char *runtime_dir);
void module_volume(const panel_config *cfg, panel_state *state);
void module_network(const panel_config *cfg, panel_state *state);
void module_brightness(const panel_config *cfg, panel_state *state);
void module_tray(const panel_config *cfg, panel_state *state);
void module_weather(const panel_config *cfg, panel_state *state);
void module_workspace(const panel_config *cfg, panel_state *state, const char *report);
void module_static(const panel_config *cfg, panel_state *state);
void render_panel(const panel_state *state, char *output, size_t size);

#endif
