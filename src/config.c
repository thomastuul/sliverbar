#include "panel.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void copy(char *dst, size_t size, const char *src) {
  if (size > 0)
    snprintf(dst, size, "%s", src ? src : "");
}

void configDefaults(PanelConfig *c) {
  memset(c, 0, sizeof(*c));
  copy(c->font, sizeof(c->font), "Monospace:style=Regular:size=11");
  copy(c->wmName, sizeof(c->wmName), "sliverbar");
  copy(c->monitor, sizeof(c->monitor), "primary");
  copy(c->workspaceBackend, sizeof(c->workspaceBackend), "auto");
  copy(c->applicationLauncher, sizeof(c->applicationLauncher), "auto");
  copy(c->powerMenuMode, sizeof(c->powerMenuMode), "auto");
  copy(c->powerActions,
       sizeof(c->powerActions),
       "lock,suspend,hibernate,suspend_then_hibernate,hybrid_sleep,reboot,"
       "poweroff");
  copy(c->powerConfirm,
       sizeof(c->powerConfirm),
       "suspend,hibernate,suspend_then_hibernate,hybrid_sleep,reboot,poweroff");
  copy(c->terminal, sizeof(c->terminal), "auto");
  copy(c->systemMonitor, sizeof(c->systemMonitor), "auto");
  copy(c->networkSettings, sizeof(c->networkSettings), "auto");
  copy(c->volumeSettings, sizeof(c->volumeSettings), "auto");
  copy(c->calendar, sizeof(c->calendar), "auto");
  copy(c->tasks, sizeof(c->tasks), "auto");
  copy(c->agendaProvider, sizeof(c->agendaProvider), "none");
  copy(c->timerSound,
       sizeof(c->timerSound),
       "/usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga");
  copy(c->language, sizeof(c->language), "auto");
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
  copy(c->agendaEventColor, 16, "#8BE9FD");
  copy(c->agendaTaskColor, 16, "#FFB86C");
  copy(c->agendaOverdueColor, 16, "#FF5555");
  copy(c->agendaSourceColor, 16, "#6272A4");
  c->height = 25;
  c->clickableAreas = 30;
  c->volumeStep = 2;
  c->brightnessStep = 5;
  c->weatherInterval = 1800;
  c->networkInterval = 60;
  c->titleMax = 45;
  c->agendaDays = 7;
  c->agendaMaxItems = 10;
  c->agendaMaxUndatedTasks = 2;
  c->agendaRefreshInterval = 300;
  c->agendaPopupWidth = 720;
  c->agendaShowSource = true;
  c->agendaCalendarSourceMode = AGENDA_SOURCES_ALL;
  c->agendaTaskSourceMode = AGENDA_SOURCES_ALL;
}

bool moduleModeActive(ModuleMode mode, bool available) {
  return mode == MODULE_ENABLED || (mode == MODULE_AUTO && available);
}

static int localeLanguage(const char *locale) {
  if (!locale || !*locale || !strcmp(locale, "C") ||
      !strncasecmp(locale, "C.", 2) || !strcmp(locale, "POSIX"))
    return -1;
  return !strncasecmp(locale, "de", 2) &&
                 (!locale[2] || strchr("_.@:-", locale[2]))
             ? 1
             : 0;
}

const char *panelLanguage(const PanelConfig *c) {
  if (c && !strcmp(c->language, "de"))
    return "de";
  if (c && !strcmp(c->language, "en"))
    return "en";
  const char *const VARIABLES[] = {"LANGUAGE", "LC_MESSAGES", "LC_ALL", "LANG"};
  for (size_t i = 0; i < sizeof(VARIABLES) / sizeof(VARIABLES[0]); i++) {
    int language = localeLanguage(getenv(VARIABLES[i]));
    if (language >= 0)
      return language ? "de" : "en";
  }
  return "en";
}

bool panelLanguageIsGerman(const PanelConfig *c) {
  return !strcmp(panelLanguage(c), "de");
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

static bool validLocationId(const char *id) {
  if (!id || !*id)
    return false;
  for (const char *cursor = id; *cursor; cursor++)
    if (!isalnum((unsigned char)*cursor) && *cursor != '_' && *cursor != '-')
      return false;
  return true;
}

static bool validColor(const char *value) {
  if (!value || value[0] != '#' || strlen(value) != 7)
    return false;
  for (size_t i = 1; i < 7; i++)
    if (!isxdigit((unsigned char)value[i]))
      return false;
  return true;
}

static bool validAgendaSourceId(const char *id) {
  if (!id || !*id || strlen(id) >= PANEL_AGENDA_SOURCE_ID_MAX ||
      !strcmp(id, "*") || !strcmp(id, "none"))
    return false;
  for (const char *cursor = id; *cursor; cursor++)
    if (iscntrl((unsigned char)*cursor))
      return false;
  return true;
}

static int addAgendaSource(PanelConfig *c, bool calendar, const char *value) {
  AgendaSourceMode *mode =
      calendar ? &c->agendaCalendarSourceMode : &c->agendaTaskSourceMode;
  bool *configured = calendar ? &c->agendaCalendarSourceConfigured
                              : &c->agendaTaskSourceConfigured;
  size_t *count =
      calendar ? &c->agendaCalendarSourceCount : &c->agendaTaskSourceCount;
  char(*sources)[PANEL_AGENDA_SOURCE_ID_MAX] =
      calendar ? c->agendaCalendarSources : c->agendaTaskSources;
  if (!*configured) {
    *configured = true;
    *count = 0;
    if (!strcmp(value, "*")) {
      *mode = AGENDA_SOURCES_ALL;
      return 0;
    }
    if (!strcmp(value, "none")) {
      *mode = AGENDA_SOURCES_NONE;
      return 0;
    }
    *mode = AGENDA_SOURCES_EXPLICIT;
  } else if (*mode != AGENDA_SOURCES_EXPLICIT) {
    return -1;
  }
  if (!validAgendaSourceId(value) || *count >= PANEL_AGENDA_SOURCE_MAX)
    return -1;
  for (size_t i = 0; i < *count; i++)
    if (!strcmp(sources[i], value))
      return -1;
  copy(sources[(*count)++], PANEL_AGENDA_SOURCE_ID_MAX, value);
  return 0;
}

static int boundedAgendaNumber(const char *key,
                               const char *value,
                               unsigned minimum,
                               unsigned maximum,
                               unsigned *destination) {
  long numberValue;
  if (number(value, LONG_MIN, LONG_MAX, &numberValue))
    return -1;
  if (numberValue < (long)minimum) {
    logMessage("WARNING",
               "%s=%ld is below the minimum of %u; using %u",
               key,
               numberValue,
               minimum,
               minimum);
    *destination = minimum;
  } else if (numberValue > (long)maximum) {
    logMessage("WARNING",
               "%s=%ld exceeds the maximum of %u; using %u",
               key,
               numberValue,
               maximum,
               maximum);
    *destination = maximum;
  } else {
    *destination = (unsigned)numberValue;
  }
  return 0;
}

static int addWeatherLocation(PanelConfig *c, const char *value) {
  if (c->weatherLocationCount >= PANEL_WEATHER_LOCATION_MAX)
    return -1;
  char copybuf[384];
  copy(copybuf, sizeof(copybuf), value);
  char *first = strchr(copybuf, '|');
  if (!first)
    return -1;
  *first++ = '\0';
  char *second = strchr(first, '|');
  if (second)
    *second++ = '\0';
  char *id = trim(copybuf);
  char *label = trim(first);
  char *query = second ? trim(second) : label;
  if (!validLocationId(id) || !*label || !*query)
    return -1;
  for (size_t i = 0; i < c->weatherLocationCount; i++)
    if (!strcmp(c->weatherLocations[i].id, id))
      return -1;
  WeatherLocation *location = &c->weatherLocations[c->weatherLocationCount++];
  copy(location->id, sizeof(location->id), id);
  copy(location->label, sizeof(location->label), label);
  copy(location->query, sizeof(location->query), query);
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
  STR("monitor", monitor);
  if (!strcmp(k, "workspace_backend")) {
    if (strcmp(v, "auto") != 0 && strcmp(v, "bspwm") != 0 &&
        strcmp(v, "ewmh") != 0 && strcmp(v, "none") != 0)
      return -1;
    copy(c->workspaceBackend, sizeof(c->workspaceBackend), v);
    return 0;
  }
  if (!strcmp(k, "application_launcher") || !strcmp(k, "power_menu_mode")) {
    if (strcmp(v, "auto") != 0 && strcmp(v, "internal") != 0 &&
        strcmp(v, "external") != 0 && strcmp(v, "disabled") != 0)
      return -1;
    bool launcher = !strcmp(k, "application_launcher");
    char *destination = launcher ? c->applicationLauncher : c->powerMenuMode;
    size_t destinationSize =
        launcher ? sizeof(c->applicationLauncher) : sizeof(c->powerMenuMode);
    copy(destination, destinationSize, v);
    return 0;
  }
  STR("terminal", terminal);
  STR("system_monitor", systemMonitor);
  STR("network_settings", networkSettings);
  STR("volume_settings", volumeSettings);
  STR("calendar", calendar);
  STR("tasks", tasks);
  if (!strcmp(k, "agenda_provider")) {
    if (strcmp(v, "none") != 0 && strcmp(v, "eds") != 0)
      return -1;
    copy(c->agendaProvider, sizeof(c->agendaProvider), v);
    return 0;
  }
  if (!strcmp(k, "agenda_calendar_source"))
    return addAgendaSource(c, true, v);
  if (!strcmp(k, "agenda_task_source"))
    return addAgendaSource(c, false, v);
  if (!strcmp(k, "agenda_show_source")) {
    if (!strcmp(v, "true"))
      c->agendaShowSource = true;
    else if (!strcmp(v, "false"))
      c->agendaShowSource = false;
    else
      return -1;
    return 0;
  }
  STR("timer_sound", timerSound);
  STR("power_actions", powerActions);
  STR("power_confirm", powerConfirm);
  STR("location", location);
  if (!strcmp(k, "weather_location"))
    return addWeatherLocation(c, v);
  STR("weather_default", defaultWeatherLocation);
  if (!strcmp(k, "language")) {
    if (strcmp(v, "auto") != 0 && strcmp(v, "de") != 0 && strcmp(v, "en") != 0)
      return -1;
    copy(c->language, sizeof(c->language), v);
    return 0;
  }
  STR("launcher", launcher);
  STR("power_menu", powerMenu);
  STR("weather_cache", weatherCache);
  if (!strcmp(k, "weather_image")) {
    copy(c->weatherImage, sizeof(c->weatherImage), v);
    logMessage("WARNING",
               "weather_image is deprecated and ignored; Sliverbar now "
               "renders the forecast natively");
    return 0;
  }
#define MODULE(key, field)                                                     \
  do {                                                                         \
    if (!strcmp(k, "module_" key)) {                                           \
      if (!strcmp(v, "auto"))                                                  \
        c->field = MODULE_AUTO;                                                \
      else if (!strcmp(v, "enabled"))                                          \
        c->field = MODULE_ENABLED;                                             \
      else if (!strcmp(v, "disabled"))                                         \
        c->field = MODULE_DISABLED;                                            \
      else                                                                     \
        return -1;                                                             \
      return 0;                                                                \
    }                                                                          \
  } while (0)
  MODULE("clock", moduleClock);
  MODULE("title", moduleTitle);
  MODULE("cpu", moduleCpu);
  MODULE("battery", moduleBattery);
  MODULE("screencast", moduleScreencast);
  MODULE("volume", moduleVolume);
  MODULE("network", moduleNetwork);
  MODULE("brightness", moduleBrightness);
  MODULE("weather", moduleWeather);
  MODULE("launcher", moduleLauncher);
  MODULE("tray", moduleTray);
  MODULE("power", modulePower);
  MODULE("inhibitor", moduleInhibitor);
  MODULE("timer", moduleTimer);
#undef MODULE
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
  if (!strcmp(k, "agenda_event_color") || !strcmp(k, "agenda_task_color") ||
      !strcmp(k, "agenda_overdue_color") || !strcmp(k, "agenda_source_color")) {
    if (!validColor(v))
      return -1;
    char *destination = !strcmp(k, "agenda_event_color")  ? c->agendaEventColor
                        : !strcmp(k, "agenda_task_color") ? c->agendaTaskColor
                        : !strcmp(k, "agenda_overdue_color")
                            ? c->agendaOverdueColor
                            : c->agendaSourceColor;
    copy(destination, 16, v);
    return 0;
  }
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
  if (!strcmp(k, "agenda_days"))
    return boundedAgendaNumber(
        k, v, PANEL_AGENDA_DAYS_MIN, PANEL_AGENDA_DAYS_MAX, &c->agendaDays);
  if (!strcmp(k, "agenda_max_items"))
    return boundedAgendaNumber(k,
                               v,
                               PANEL_AGENDA_ITEMS_MIN,
                               PANEL_AGENDA_ITEMS_MAX,
                               &c->agendaMaxItems);
  if (!strcmp(k, "agenda_max_undated_tasks"))
    return boundedAgendaNumber(
        k, v, 0, PANEL_AGENDA_UNDATED_MAX, &c->agendaMaxUndatedTasks);
  if (!strcmp(k, "agenda_refresh_interval"))
    return boundedAgendaNumber(k,
                               v,
                               PANEL_AGENDA_REFRESH_MIN,
                               PANEL_AGENDA_REFRESH_MAX,
                               &c->agendaRefreshInterval);
  if (!strcmp(k, "agenda_popup_width"))
    return boundedAgendaNumber(k,
                               v,
                               PANEL_AGENDA_WIDTH_MIN,
                               PANEL_AGENDA_WIDTH_MAX,
                               &c->agendaPopupWidth);
  if (!strcmp(k, "weather_interval")) {
    if (number(v, LONG_MIN, LONG_MAX, &n))
      return -1;
    if (n < (long)PANEL_WEATHER_INTERVAL_MIN) {
      logMessage("WARNING",
                 "weather_interval=%ld is below the minimum of %u seconds; "
                 "using %u seconds",
                 n,
                 PANEL_WEATHER_INTERVAL_MIN,
                 PANEL_WEATHER_INTERVAL_MIN);
      c->weatherInterval = PANEL_WEATHER_INTERVAL_MIN;
    } else if (n > (long)PANEL_WEATHER_INTERVAL_MAX) {
      logMessage("WARNING",
                 "weather_interval=%ld exceeds the maximum of %u seconds; "
                 "using %u seconds",
                 n,
                 PANEL_WEATHER_INTERVAL_MAX,
                 PANEL_WEATHER_INTERVAL_MAX);
      c->weatherInterval = PANEL_WEATHER_INTERVAL_MAX;
    } else {
      c->weatherInterval = (unsigned)n;
    }
    return 0;
  }
  if (!strcmp(k, "network_interval") || !strcmp(k, "title_max")) {
    if (number(v, 1, 86400, &n))
      return -1;
    if (!strcmp(k, "network_interval"))
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
  const char *const POWER_LISTS[] = {c->powerActions, c->powerConfirm};
  static const char *const POWER_IDS[] = {"lock",
                                          "suspend",
                                          "hibernate",
                                          "suspend_then_hibernate",
                                          "hybrid_sleep",
                                          "reboot",
                                          "poweroff"};
  for (size_t listIndex = 0; !rc && listIndex < 2; listIndex++) {
    char list[256];
    copy(list, sizeof(list), POWER_LISTS[listIndex]);
    char *save = NULL;
    bool seen[7] = {0};
    for (char *id = strtok_r(list, ",", &save); id;
         id = strtok_r(NULL, ",", &save)) {
      id = trim(id);
      bool found = false;
      for (size_t i = 0; i < 7; i++)
        if (!strcmp(id, POWER_IDS[i])) {
          if (seen[i])
            break;
          seen[i] = true;
          found = true;
          break;
        }
      if (!found) {
        snprintf(error,
                 errorSize,
                 "%s: invalid %s",
                 path,
                 listIndex == 0 ? "power_actions" : "power_confirm");
        rc = -1;
        break;
      }
    }
  }
  if (!rc && c->weatherLocationCount) {
    size_t selected = 0;
    if (c->defaultWeatherLocation[0]) {
      bool found = false;
      for (size_t i = 0; i < c->weatherLocationCount; i++)
        if (!strcmp(c->weatherLocations[i].id, c->defaultWeatherLocation)) {
          selected = i;
          found = true;
          break;
        }
      if (!found) {
        snprintf(error, errorSize, "%s: unknown weather_default", path);
        return -1;
      }
    }
    c->activeWeatherLocation = selected;
    copy(c->location, sizeof(c->location), c->weatherLocations[selected].query);
  } else if (!rc && c->location[0]) {
    c->weatherLocationCount = 1;
    copy(c->weatherLocations[0].id,
         sizeof(c->weatherLocations[0].id),
         "default");
    copy(c->weatherLocations[0].label,
         sizeof(c->weatherLocations[0].label),
         c->location);
    copy(c->weatherLocations[0].query,
         sizeof(c->weatherLocations[0].query),
         c->location);
  }
  return rc;
}
