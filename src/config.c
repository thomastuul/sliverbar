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

void configDefaults(PanelConfig *c) {
  memset(c, 0, sizeof(*c));
  copy(c->font, sizeof(c->font), "JetBrainsMono:style=Regular:size=13");
  copy(c->iconFont,
       sizeof(c->iconFont),
       "Hack Nerd Font Mono:style=Regular:size=13");
  copy(c->wmName, sizeof(c->wmName), "sliverbar");
  copy(c->terminal, sizeof(c->terminal), "alacritty");
  copy(c->location, sizeof(c->location), "München");
  copy(c->language, sizeof(c->language), "de");
  copy(c->colorPanelBg, 16, "#191A21");
  copy(c->colorBg, 16, "#282A36");
  copy(c->colorFg, 16, "#ff5555");
  copy(c->colorFree, 16, "#bfbfbf");
  copy(c->colorFocus, 16, "#69FF94");
  copy(c->colorFreeBg, 16, "#191A21");
  copy(c->colorFocusedFree, 16, "#50fa7b");
  copy(c->colorFocusedFreeBg, 16, "#21222C");
  copy(c->colorOccupied, 16, "#ff5555");
  copy(c->colorOccupiedBg, 16, "#191A21");
  copy(c->colorFocusedOccupied, 16, "#69FF94");
  copy(c->colorFocusedOccupiedBg, 16, "#191A21");
  copy(c->colorUrgent, 16, "#FF6E6E");
  copy(c->colorUrgentBg, 16, "#343746");
  copy(c->colorFocusedUrgent, 16, "#343746");
  copy(c->colorFocusedUrgentBg, 16, "#FF6E6E");
  copy(c->colorClock, 16, "#50fa7b");
  copy(c->colorVolume, 16, "#ff79c6");
  copy(c->colorMuted, 16, "#ff5555");
  copy(c->colorSystem, 16, "#f1fa8c");
  copy(c->colorNetwork, 16, "#8be9fd");
  copy(c->colorWeather, 16, "#bd93f9");
  copy(c->colorBattery, 16, "#ffb86c");
  copy(c->colorWarning, 16, "#f1fa8c");
  copy(c->colorCritical, 16, "#ff5555");
  copy(c->colorBrightness, 16, "#bd93f9");
  c->height = 25;
  c->clickableAreas = 30;
  c->volumeStep = 2;
  c->brightnessStep = 5;
  c->weatherInterval = 1800;
  c->networkInterval = 60;
  c->titleMax = 45;
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

static int assign(PanelConfig *c, const char *k, const char *v) {
#define STR(key, field)                                                        \
  do {                                                                         \
    if (!strcmp(k, key)) {                                                     \
      copy(c->field, sizeof(c->field), v);                                     \
      return 0;                                                                \
    }                                                                          \
  } while (0)
  STR("font", font);
  STR("icon_font", iconFont);
  STR("wm_name", wmName);
  STR("terminal", terminal);
  STR("location", location);
  STR("language", language);
  STR("launcher", launcher);
  STR("power_menu", powerMenu);
  STR("weather_cache", weatherCache);
  STR("weather_image", weatherImage);
  STR("color_panel_bg", colorPanelBg);
  STR("color_bg", colorBg);
  STR("color_fg", colorFg);
  STR("color_free", colorFree);
  STR("color_focus", colorFocus);
  STR("color_free_bg", colorFreeBg);
  STR("color_focused_free", colorFocusedFree);
  STR("color_focused_free_bg", colorFocusedFreeBg);
  STR("color_occupied", colorOccupied);
  STR("color_occupied_bg", colorOccupiedBg);
  STR("color_focused_occupied", colorFocusedOccupied);
  STR("color_focused_occupied_bg", colorFocusedOccupiedBg);
  STR("color_urgent", colorUrgent);
  STR("color_urgent_bg", colorUrgentBg);
  STR("color_focused_urgent", colorFocusedUrgent);
  STR("color_focused_urgent_bg", colorFocusedUrgentBg);
  STR("color_clock", colorClock);
  STR("color_volume", colorVolume);
  STR("color_muted", colorMuted);
  STR("color_system", colorSystem);
  STR("color_network", colorNetwork);
  STR("color_weather", colorWeather);
  STR("color_battery", colorBattery);
  STR("color_warning", colorWarning);
  STR("color_critical", colorCritical);
  STR("color_brightness", colorBrightness);
#undef STR
  long n;
#define NUM(key, field, min, max)                                              \
  do {                                                                         \
    if (!strcmp(k, key)) {                                                     \
      if (number(v, min, max, &n))                                             \
        return -1;                                                             \
      c->field = (int)n;                                                       \
      return 0;                                                                \
    }                                                                          \
  } while (0)
  NUM("height", height, 1, 512);
  NUM("clickable_areas", clickableAreas, 1, 255);
  NUM("underline", underline, 0, 32);
  NUM("volume_step", volumeStep, 1, 100);
  NUM("brightness_step", brightnessStep, 1, 100);
#undef NUM
  if (!strcmp(k, "weather_interval") || !strcmp(k, "network_interval") ||
      !strcmp(k, "title_max")) {
    if (number(v, 1, 86400, &n))
      return -1;
    if (!strcmp(k, "weather_interval"))
      c->weatherInterval = (unsigned)n;
    else if (!strcmp(k, "network_interval"))
      c->networkInterval = (unsigned)n;
    else
      c->titleMax = (unsigned)n;
    return 0;
  }
  return 1;
}

int configLoad(PanelConfig *c,
               const char *path,
               char *error,
               size_t errorSize) {
  FILE *f = fopen(path, "r");
  if (!f) {
    snprintf(error, errorSize, "%s: %s", path, strerror(errno));
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
      snprintf(error, errorSize, "%s:%u: expected key=value", path, lineno);
      rc = -1;
      break;
    }
    *eq = '\0';
    int a = assign(c, trim(p), trim(eq + 1));
    if (a) {
      snprintf(error,
               errorSize,
               "%s:%u: %s key",
               path,
               lineno,
               a < 0 ? "invalid" : "unknown");
      rc = -1;
      break;
    }
  }
  if (ferror(f) && !rc) {
    snprintf(error, errorSize, "%s: read failed", path);
    rc = -1;
  }
  fclose(f);
  return rc;
}
