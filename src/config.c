#include "panel.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy(char *dst, size_t size, const char *src) {
    if (size > 0)
        snprintf(dst, size, "%s", src ? src : "");
}

void config_defaults(panel_config *c) {
    memset(c, 0, sizeof(*c));
    copy(c->font, sizeof(c->font), "JetBrainsMono:style=Regular:size=11");
    copy(c->icon_font, sizeof(c->icon_font), "Hack Nerd Font Mono:style=Regular:size=11");
    copy(c->wm_name, sizeof(c->wm_name), "lemonbar-c");
    copy(c->terminal, sizeof(c->terminal), "alacritty");
    copy(c->location, sizeof(c->location), "München");
    copy(c->language, sizeof(c->language), "de");
    copy(c->color_panel_bg, 16, "#191A21");
    copy(c->color_bg, 16, "#282A36");
    copy(c->color_fg, 16, "#ff5555");
    copy(c->color_free, 16, "#bfbfbf");
    copy(c->color_focus, 16, "#69FF94");
    copy(c->color_free_bg, 16, "#191A21");
    copy(c->color_focused_free, 16, "#50fa7b");
    copy(c->color_focused_free_bg, 16, "#21222C");
    copy(c->color_occupied, 16, "#ff5555");
    copy(c->color_occupied_bg, 16, "#191A21");
    copy(c->color_focused_occupied, 16, "#69FF94");
    copy(c->color_focused_occupied_bg, 16, "#191A21");
    copy(c->color_urgent, 16, "#FF6E6E");
    copy(c->color_urgent_bg, 16, "#343746");
    copy(c->color_focused_urgent, 16, "#343746");
    copy(c->color_focused_urgent_bg, 16, "#FF6E6E");
    copy(c->color_clock, 16, "#50fa7b");
    copy(c->color_volume, 16, "#ff79c6");
    copy(c->color_muted, 16, "#ff5555");
    copy(c->color_system, 16, "#f1fa8c");
    copy(c->color_network, 16, "#8be9fd");
    copy(c->color_weather, 16, "#bd93f9");
    copy(c->color_battery, 16, "#ffb86c");
    copy(c->color_warning, 16, "#f1fa8c");
    copy(c->color_critical, 16, "#ff5555");
    copy(c->color_brightness, 16, "#bd93f9");
    c->height = 25;
    c->clickable_areas = 30;
    c->volume_step = 2;
    c->brightness_step = 5;
    c->weather_interval = 1800;
    c->network_interval = 60;
    c->title_max = 45;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static int number(const char *s, long min, long max, long *out) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *trim(end) || v < min || v > max)
        return -1;
    *out = v;
    return 0;
}

static int assign(panel_config *c, const char *k, const char *v) {
#define STR(key, field)                                                                            \
    do {                                                                                           \
        if (!strcmp(k, key)) {                                                                     \
            copy(c->field, sizeof(c->field), v);                                                   \
            return 0;                                                                              \
        }                                                                                          \
    } while (0)
    STR("font", font);
    STR("icon_font", icon_font);
    STR("wm_name", wm_name);
    STR("terminal", terminal);
    STR("location", location);
    STR("language", language);
    STR("launcher", launcher);
    STR("power_menu", power_menu);
    STR("weather_cache", weather_cache);
    STR("weather_image", weather_image);
    STR("color_panel_bg", color_panel_bg);
    STR("color_bg", color_bg);
    STR("color_fg", color_fg);
    STR("color_free", color_free);
    STR("color_focus", color_focus);
    STR("color_free_bg", color_free_bg);
    STR("color_focused_free", color_focused_free);
    STR("color_focused_free_bg", color_focused_free_bg);
    STR("color_occupied", color_occupied);
    STR("color_occupied_bg", color_occupied_bg);
    STR("color_focused_occupied", color_focused_occupied);
    STR("color_focused_occupied_bg", color_focused_occupied_bg);
    STR("color_urgent", color_urgent);
    STR("color_urgent_bg", color_urgent_bg);
    STR("color_focused_urgent", color_focused_urgent);
    STR("color_focused_urgent_bg", color_focused_urgent_bg);
    STR("color_clock", color_clock);
    STR("color_volume", color_volume);
    STR("color_muted", color_muted);
    STR("color_system", color_system);
    STR("color_network", color_network);
    STR("color_weather", color_weather);
    STR("color_battery", color_battery);
    STR("color_warning", color_warning);
    STR("color_critical", color_critical);
    STR("color_brightness", color_brightness);
#undef STR
    long n;
#define NUM(key, field, min, max)                                                                  \
    do {                                                                                           \
        if (!strcmp(k, key)) {                                                                     \
            if (number(v, min, max, &n))                                                           \
                return -1;                                                                         \
            c->field = (int)n;                                                                     \
            return 0;                                                                              \
        }                                                                                          \
    } while (0)
    NUM("height", height, 1, 512);
    NUM("clickable_areas", clickable_areas, 1, 255);
    NUM("underline", underline, 0, 32);
    NUM("volume_step", volume_step, 1, 100);
    NUM("brightness_step", brightness_step, 1, 100);
#undef NUM
    if (!strcmp(k, "weather_interval") || !strcmp(k, "network_interval") ||
        !strcmp(k, "title_max")) {
        if (number(v, 1, 86400, &n))
            return -1;
        if (!strcmp(k, "weather_interval"))
            c->weather_interval = (unsigned)n;
        else if (!strcmp(k, "network_interval"))
            c->network_interval = (unsigned)n;
        else
            c->title_max = (unsigned)n;
        return 0;
    }
    return 1;
}

int config_load(panel_config *c, const char *path, char *error, size_t error_size) {
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(error, error_size, "%s: %s", path, strerror(errno));
        return -1;
    }
    char line[1024];
    unsigned lineno = 0;
    int rc = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *p = trim(line);
        if (!*p || *p == '#')
            continue;
        char *eq = strchr(p, '=');
        if (!eq) {
            snprintf(error, error_size, "%s:%u: expected key=value", path, lineno);
            rc = -1;
            break;
        }
        *eq = '\0';
        int a = assign(c, trim(p), trim(eq + 1));
        if (a) {
            snprintf(
                error, error_size, "%s:%u: %s key", path, lineno, a < 0 ? "invalid" : "unknown");
            rc = -1;
            break;
        }
    }
    if (ferror(f) && !rc) {
        snprintf(error, error_size, "%s: read failed", path);
        rc = -1;
    }
    fclose(f);
    return rc;
}
