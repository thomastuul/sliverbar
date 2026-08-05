#include "agenda_provider.h"
#include "control_ipc.h"
#include "native_panel.h"
#include "native_popup.h"
#include "panel.h"
#include "power_actions.h"
#include "power_profiles.h"
#include "timer.h"
#include "version.h"
#include "weather_forecast.h"
#include "workspace_backend.h"

#include "app_launcher.h"
#include "inhibitor.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_XCB
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#endif

static int
joinPath(char *out, size_t size, const char *base, const char *suffix) {
  size_t a = strlen(base), b = strlen(suffix);
  if (a + b + 1 > size)
    return -1;
  memcpy(out, base, a);
  memcpy(out + a, suffix, b + 1);
  return 0;
}

static const char *timerSoundBackend(void) {
  static const char *const BACKENDS[] = {
      "pw-play", "paplay", "canberra-gtk-play", "aplay"};
  for (size_t i = 0; i < sizeof(BACKENDS) / sizeof(BACKENDS[0]); i++)
    if (commandExists(BACKENDS[i]))
      return BACKENDS[i];
  return NULL;
}

#ifdef HAVE_NATIVE_PANEL
typedef struct {
  pid_t pid;
  int readFd, writeFd;
} Child;

static int childPipe(char *const argv[], bool input, Child *out) {
  int p[2];
  if (pipe2(p, O_CLOEXEC | O_NONBLOCK))
    return -1;
  pid_t pid = fork();
  if (pid < 0) {
    close(p[0]);
    close(p[1]);
    return -1;
  }
  if (!pid) {
    if (input)
      dup2(p[0], STDIN_FILENO);
    else
      dup2(p[1], STDOUT_FILENO);
    close(p[0]);
    close(p[1]);
    execvp(argv[0], argv);
    _exit(127);
  }
  out->pid = pid;
  if (input) {
    close(p[0]);
    out->writeFd = p[1];
    out->readFd = -1;
  } else {
    close(p[1]);
    out->readFd = p[0];
    out->writeFd = -1;
  }
  return 0;
}

static void stopChild(Child *c) {
  if (c->pid > 0) {
    kill(c->pid, SIGTERM);
    for (int i = 0; i < 20 && waitpid(c->pid, NULL, WNOHANG) == 0; i++)
      usleep(10000);
    if (waitpid(c->pid, NULL, WNOHANG) == 0) {
      kill(c->pid, SIGKILL);
      waitpid(c->pid, NULL, 0);
    }
  }
  if (c->readFd >= 0)
    close(c->readFd);
  if (c->writeFd >= 0)
    close(c->writeFd);
  memset(c, 0, sizeof(*c));
  c->readFd = c->writeFd = -1;
}

#ifndef HAVE_XCB
static void retire_child(child *c) {
  if (c->pid > 0)
    kill(c->pid, SIGTERM);
  if (c->read_fd >= 0)
    close(c->read_fd);
  if (c->write_fd >= 0)
    close(c->write_fd);
  c->pid = 0;
  c->read_fd = c->write_fd = -1;
}
#endif

static void storeTitle(const char *title,
                       unsigned max,
                       PanelState *s,
                       const PanelConfig *c) {
  s->title[0] = '\0';
  if (!moduleModeActive(c->moduleTitle, true))
    return;
  char clipped[512], safe[512];
  const char *display = title && *title ? title : "Desktop";
  snprintf(clipped, sizeof(clipped), "%.*s", (int)max, display);
  shellQuoteAction(clipped, safe, sizeof(safe));
  snprintf(s->title,
           sizeof(s->title),
           "%%{B%s}%%{F%s}%%{+u} %s %%{-u}%%{F-}%%{B-}",
           c->colorBg,
           c->colorFree,
           safe);
  if (getenv("SLIVERBAR_DEBUG"))
    logMessage("DEBUG", "title=%s", clipped);
}

#ifdef HAVE_XCB
static xcb_atom_t atom(xcb_connection_t *x, const char *name) {
  xcb_intern_atom_cookie_t ck =
      xcb_intern_atom(x, 0, (uint16_t)strlen(name), name);
  xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(x, ck, NULL);
  xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
  free(r);
  return a;
}

static xcb_keycode_t keycodeForKeysym(xcb_connection_t *connection,
                                      xcb_keysym_t requested) {
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_keycode_t first = setup->min_keycode;
  uint8_t count = (uint8_t)(setup->max_keycode - first + 1);
  xcb_get_keyboard_mapping_reply_t *mapping = xcb_get_keyboard_mapping_reply(
      connection, xcb_get_keyboard_mapping(connection, first, count), NULL);
  if (!mapping)
    return 0;
  xcb_keysym_t *symbols = xcb_get_keyboard_mapping_keysyms(mapping);
  size_t total = (size_t)count * mapping->keysyms_per_keycode;
  xcb_keycode_t result = 0;
  for (size_t index = 0; index < total; index++)
    if (symbols[index] == requested) {
      result = (xcb_keycode_t)(first + index / mapping->keysyms_per_keycode);
      break;
    }
  free(mapping);
  return result;
}

static xcb_window_t
activeWindow(xcb_connection_t *x, xcb_window_t root, xcb_atom_t active) {
  xcb_get_property_reply_t *reply = xcb_get_property_reply(
      x, xcb_get_property(x, 0, root, active, XCB_ATOM_WINDOW, 0, 1), NULL);
  xcb_window_t window = XCB_WINDOW_NONE;
  if (reply && reply->format == 32 && xcb_get_property_value_length(reply) >= 4)
    memcpy(&window, xcb_get_property_value(reply), sizeof(window));
  free(reply);
  return window;
}

static xcb_get_property_reply_t *windowName(xcb_connection_t *x,
                                            xcb_window_t window,
                                            xcb_atom_t netName,
                                            xcb_atom_t utf8) {
  xcb_get_property_reply_t *reply = xcb_get_property_reply(
      x, xcb_get_property(x, 0, window, netName, utf8, 0, 1024), NULL);
  if (reply && xcb_get_property_value_length(reply) > 0)
    return reply;
  free(reply);
  return xcb_get_property_reply(
      x,
      xcb_get_property(
          x, 0, window, XCB_ATOM_WM_NAME, XCB_GET_PROPERTY_TYPE_ANY, 0, 1024),
      NULL);
}

static bool windowClassName(xcb_connection_t *x,
                            xcb_window_t window,
                            char *output,
                            size_t size) {
  xcb_get_property_reply_t *reply = xcb_get_property_reply(
      x,
      xcb_get_property(
          x, 0, window, XCB_ATOM_WM_CLASS, XCB_GET_PROPERTY_TYPE_ANY, 0, 1024),
      NULL);
  int length = reply ? xcb_get_property_value_length(reply) : 0;
  const char *value = reply ? xcb_get_property_value(reply) : NULL;
  if (!reply || !value || length <= 0 || size == 0) {
    free(reply);
    return false;
  }
  size_t firstLength = strnlen(value, (size_t)length);
  const char *label = value;
  size_t labelLength = firstLength;
  if (firstLength + 1 < (size_t)length) {
    const char *className = value + firstLength + 1;
    size_t classLength = strnlen(className, (size_t)length - firstLength - 1);
    if (classLength > 0) {
      label = className;
      labelLength = classLength;
    }
  }
  if (labelLength == 0) {
    free(reply);
    return false;
  }
  if (labelLength >= size)
    labelLength = size - 1;
  memcpy(output, label, labelLength);
  output[labelLength] = '\0';
  free(reply);
  return true;
}

static void
titleUnavailable(unsigned max, PanelState *state, const PanelConfig *config) {
  if (state->focusedWorkspaceKnown && !state->focusedWorkspaceOccupied)
    storeTitle("", max, state, config);
}

static void updateTitleXcb(xcb_connection_t *x,
                           xcb_window_t root,
                           xcb_atom_t active,
                           xcb_atom_t utf8,
                           xcb_atom_t netName,
                           unsigned max,
                           PanelState *s,
                           const PanelConfig *c) {
  xcb_window_t win = activeWindow(x, root, active);
  if (win == XCB_WINDOW_NONE) {
    usleep(10000);
    win = activeWindow(x, root, active);
    if (win == XCB_WINDOW_NONE) {
      titleUnavailable(max, s, c);
      return;
    }
  }
  uint32_t events = XCB_EVENT_MASK_PROPERTY_CHANGE;
  xcb_change_window_attributes(x, win, XCB_CW_EVENT_MASK, &events);
  xcb_flush(x);
  xcb_get_property_reply_t *r = windowName(x, win, netName, utf8);
  if (!r || xcb_get_property_value_length(r) <= 0) {
    free(r);
    r = NULL;
    usleep(10000);
    win = activeWindow(x, root, active);
    if (win != XCB_WINDOW_NONE) {
      xcb_change_window_attributes(x, win, XCB_CW_EVENT_MASK, &events);
      xcb_flush(x);
      r = windowName(x, win, netName, utf8);
    }
  }
  if (!r || xcb_get_property_value_length(r) <= 0) {
    free(r);
    char className[512];
    if (win != XCB_WINDOW_NONE &&
        windowClassName(x, win, className, sizeof(className))) {
      storeTitle(className, max, s, c);
      return;
    }
    titleUnavailable(max, s, c);
    return;
  }
  int len = xcb_get_property_value_length(r);
  char title[512];
  if ((size_t)len >= sizeof(title))
    len = (int)sizeof(title) - 1;
  memcpy(title, xcb_get_property_value(r), (size_t)len);
  title[len] = '\0';
  free(r);
  storeTitle(title, max, s, c);
}
#else
static int active_window_id(char *id, size_t size) {
  char out[2048];
  char *active[] = {"xprop", "-root", "_NET_ACTIVE_WINDOW", NULL};
  if (run_capture(active, out, sizeof(out), 500))
    return -1;
  char *hash = strchr(out, '#');
  if (!hash)
    return -1;
  hash++;
  while (isspace((unsigned char)*hash))
    hash++;
  snprintf(id, size, "%.24s", hash);
  char *space = strpbrk(id, " \t\r\n");
  if (space)
    *space = '\0';
  return !*id || !strcmp(id, "0x0") ? -1 : 0;
}

static void update_title_for_id(char *id,
                                unsigned max,
                                panel_state *s,
                                const panel_config *c) {
  char out[2048];
  char *name[] = {"xprop", "-id", id, "_NET_WM_NAME", "WM_NAME", NULL};
  if (run_capture(name, out, sizeof(out), 500))
    return;
  char *first = strchr(out, '\"');
  if (first) {
    char *last = strchr(first + 1, '\"');
    if (last) {
      *last = '\0';
      store_title(first + 1, max, s, c);
      return;
    }
  }
  store_title("", max, s, c);
}

static void
update_title_fallback(unsigned max, panel_state *s, const panel_config *c) {
  char id[32];
  if (active_window_id(id, sizeof(id))) {
    store_title("", max, s, c);
    return;
  }
  update_title_for_id(id, max, s, c);
}

static int start_window_title_watcher_for_id(child *watcher, char *id) {
  if (command_exists("stdbuf")) {
    char *args[] = {"stdbuf",
                    "-oL",
                    "xprop",
                    "-spy",
                    "-id",
                    id,
                    "_NET_WM_NAME",
                    "WM_NAME",
                    NULL};
    return child_pipe(args, false, watcher);
  }
  char *args[] = {"xprop", "-spy", "-id", id, "_NET_WM_NAME", "WM_NAME", NULL};
  return child_pipe(args, false, watcher);
}

static int start_window_title_watcher(child *watcher) {
  char id[32];
  return active_window_id(id, sizeof(id))
             ? -1
             : start_window_title_watcher_for_id(watcher, id);
}

static int start_active_window_watcher(child *watcher) {
  if (command_exists("stdbuf")) {
    char *args[] = {
        "stdbuf", "-oL", "xprop", "-spy", "-root", "_NET_ACTIVE_WINDOW", NULL};
    return child_pipe(args, false, watcher);
  }
  char *args[] = {"xprop", "-spy", "-root", "_NET_ACTIVE_WINDOW", NULL};
  return child_pipe(args, false, watcher);
}

static int active_id_from_event(char *event, char *id, size_t size) {
  char *hash = strrchr(event, '#');
  if (!hash)
    return -1;
  hash++;
  while (isspace((unsigned char)*hash))
    hash++;
  snprintf(id, size, "%.24s", hash);
  char *space = strpbrk(id, " \t\r\n");
  if (space)
    *space = '\0';
  return !*id || !strcmp(id, "0x0") ? -1 : 0;
}

static void title_from_event(char *event,
                             unsigned max,
                             panel_state *s,
                             const panel_config *c) {
  char *property = NULL;
  for (char *next = event; (next = strstr(next, "_NET_WM_NAME")); next++)
    property = next;
  if (!property)
    property = event;
  char *first = strchr(property, '\"');
  if (!first) {
    store_title("", max, s, c);
    return;
  }
  char *last = strchr(first + 1, '\"');
  if (!last) {
    store_title("", max, s, c);
    return;
  }
  *last = '\0';
  store_title(first + 1, max, s, c);
}
#endif

static int setVolume(const PanelConfig *c, const char *op) {
  char ignored[256];
  if (!op || (strcmp(op, "up") != 0 && strcmp(op, "down") != 0 &&
              strcmp(op, "toggle") != 0))
    return -1;
  if (commandExists("pactl")) {
    char value[32];
    if (!strcmp(op, "up")) {
      char current[2048];
      char *get[] = {"pactl", "get-sink-volume", "@DEFAULT_SINK@", NULL};
      if (runCapture(get, current, sizeof(current), 1500) ||
          !pactlVolumeArgument(
              current, c->volumeStep, op, value, sizeof(value)))
        return -1;
    } else if (!strcmp(op, "down")) {
      if (!pactlVolumeArgument(NULL, c->volumeStep, op, value, sizeof(value)))
        return -1;
    } else if (!strcmp(op, "toggle")) {
      char *av[] = {"pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle", NULL};
      if (!runCapture(av, ignored, sizeof(ignored), 1500))
        return 0;
      if (commandExists("amixer")) {
        char *mv[] = {"amixer", "set", "Master", "toggle", NULL};
        return runCapture(mv, ignored, sizeof(ignored), 1500);
      }
      return -1;
    }
    char *av[] = {"pactl", "set-sink-volume", "@DEFAULT_SINK@", value, NULL};
    return runCapture(av, ignored, sizeof(ignored), 1500);
  }
  char value[32];
  snprintf(value,
           sizeof(value),
           !strcmp(op, "up")     ? "%d%%+"
           : !strcmp(op, "down") ? "%d%%-"
                                 : "toggle",
           c->volumeStep);
  char *av[] = {"amixer", "set", "Master", value, NULL};
  return runCapture(av, ignored, sizeof(ignored), 1500);
}

static int applyBrightness(PanelState *s) {
  return moduleBrightnessApply(s);
}

static int scheduleBrightness(const PanelConfig *c,
                              PanelState *s,
                              const char *operation,
                              int timerFd) {
  if (!moduleBrightnessAdjust(c, s, operation))
    return 0;
  struct itimerspec debounce = {{0, 0}, {0, 75000000}};
  if (timerfd_settime(timerFd, 0, &debounce, NULL)) {
    moduleBrightness(c, s);
    return -1;
  }
  s->brightnessUpdatePending = true;
  return 0;
}

static void refreshWeather(const PanelConfig *c) {
  if (!*c->weatherCache || !commandExists("curl"))
    return;
  char location[256], url[512];
  snprintf(location, sizeof(location), "%s", c->location);
  for (char *p = location; *p; p++)
    if (*p == ' ')
      *p = '+';
  snprintf(url,
           sizeof(url),
           "https://wttr.in/%s?format=j1&lang=%s",
           location,
           panelLanguage(c));
  char data[32768];
  char *av[] = {
      "curl", "-fsSL", "--connect-timeout", "3", "--max-time", "15", url, NULL};
  if (!runCapture(av, data, sizeof(data), 18000) && *data) {
    char parent[PANEL_PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", c->weatherCache);
    char *slash = strrchr(parent, '/');
    if (slash) {
      *slash = '\0';
      mkdirP(parent, 0700);
    }
    if (writeAtomic(c->weatherCache, data, 0600))
      logMessage("ERROR", "cannot publish weather cache: %s", strerror(errno));
  }
}

static pid_t startWeatherRefresh(const PanelConfig *c) {
  if (!moduleModeActive(c->moduleWeather, c->location[0] != '\0') ||
      !c->location[0] || !commandExists("curl"))
    return 0;
  pid_t pid = fork();
  if (pid == 0) {
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
    refreshWeather(c);
    _exit(0);
  }
  return pid;
}

static void updateWeatherPaths(PanelConfig *config) {
  if (!config->weatherCacheRoot[0] || !config->weatherLocationCount)
    return;
  const char *id = config->weatherLocations[config->activeWeatherLocation].id;
  char suffix[96];
  snprintf(suffix, sizeof(suffix), "/%s.json", id);
  joinPath(config->weatherCache,
           sizeof(config->weatherCache),
           config->weatherCacheRoot,
           suffix);
}

static bool selectWeatherLocation(PanelConfig *config, const char *id) {
  for (size_t i = 0; i < config->weatherLocationCount; i++) {
    if (strcmp(config->weatherLocations[i].id, id) != 0)
      continue;
    config->activeWeatherLocation = i;
    snprintf(config->location,
             sizeof(config->location),
             "%s",
             config->weatherLocations[i].query);
    updateWeatherPaths(config);
    return true;
  }
  return false;
}

static int openApplicationLauncher(NativePopup *popup) {
  if (!popup || !appLauncherHasGio())
    return -1;
  AppEntry *applications = calloc(POPUP_ITEM_MAX, sizeof(*applications));
  PopupItem *items = calloc(POPUP_ITEM_MAX, sizeof(*items));
  if (!applications || !items) {
    free(items);
    free(applications);
    return -1;
  }
  size_t count = appCatalogLoad(applications, POPUP_ITEM_MAX);
  for (size_t i = 0; i < count; i++) {
    snprintf(
        items[i].label, sizeof(items[i].label), "%s", applications[i].name);
    snprintf(
        items[i].search, sizeof(items[i].search), "%s", applications[i].search);
    char safeId[240];
    shellQuoteAction(applications[i].desktopId, safeId, sizeof(safeId));
    snprintf(items[i].action, sizeof(items[i].action), "app|%s", safeId);
  }
  int result = nativePopupOpen(popup, items, count, true, false);
  free(items);
  free(applications);
  return result;
}

static int openPowerMenu(NativePopup *popup, const PanelConfig *config) {
  PowerAction actions[16];
  PopupItem items[16] = {0};
  size_t count = powerActionList(config,
                                 config->powerActions,
                                 actions,
                                 sizeof(actions) / sizeof(actions[0]));
  if (count > sizeof(actions) / sizeof(actions[0]))
    count = sizeof(actions) / sizeof(actions[0]);
  for (size_t i = 0; i < count; i++) {
    snprintf(items[i].label,
             sizeof(items[i].label),
             "%.15s  %.63s",
             actions[i].glyph,
             actions[i].label);
    snprintf(items[i].search,
             sizeof(items[i].search),
             "%.15s %.63s",
             actions[i].glyph,
             actions[i].label);
    snprintf(items[i].action,
             sizeof(items[i].action),
             "power_action|%.31s",
             actions[i].id);
  }
  return nativePopupOpen(popup, items, count, false, true);
}

static int openPowerProfiles(NativePopup *popup,
                             NativePanel *panel,
                             const PanelConfig *config) {
  PowerProfileState state;
  if (powerProfilesQuery(config, &state))
    return -1;
  PopupItem items[POWER_PROFILE_MAX] = {0};
  for (size_t i = 0; i < state.count; i++) {
    snprintf(items[i].label,
             sizeof(items[i].label),
             "%s%s",
             state.profiles[i].active ? "✓  " : "   ",
             state.profiles[i].label);
    snprintf(items[i].search,
             sizeof(items[i].search),
             "%s %s",
             state.profiles[i].id,
             state.profiles[i].label);
    snprintf(items[i].action,
             sizeof(items[i].action),
             "power_profile|%.63s",
             state.profiles[i].id);
  }
  int actionX = 0, actionWidth = 1;
  if (!nativePanelActionBounds(
          panel, "power_profile|menu", &actionX, &actionWidth)) {
    int panelY = 0, panelWidth = 0, panelHeight = 0;
    nativePanelBounds(panel, &actionX, &panelY, &panelWidth, &panelHeight);
  }
  return nativePopupOpenAt(
      popup, items, state.count, false, actionX, actionWidth);
}

static bool sleepPowerAction(const char *id) {
  return !strcmp(id, "suspend") || !strcmp(id, "hibernate") ||
         !strcmp(id, "suspend_then_hibernate") || !strcmp(id, "hybrid_sleep");
}

static int openPowerConfirmation(NativePopup *popup,
                                 const PanelConfig *config,
                                 const char *id,
                                 bool inhibitorConflict) {
  PopupItem items[2] = {0};
  bool german = panelLanguageIsGerman(config);
  snprintf(items[0].label,
           sizeof(items[0].label),
           "%s",
           german ? "Abbrechen" : "Cancel");
  snprintf(items[0].search, sizeof(items[0].search), "cancel no");
  snprintf(items[0].action, sizeof(items[0].action), "power_cancel|cancel");
  snprintf(items[1].label,
           sizeof(items[1].label),
           german ? "%s%s bestätigen" : "%s%s",
           inhibitorConflict
               ? (german ? "Inhibitor lösen und " : "Release inhibitor and ")
               : (german ? "" : "Confirm "),
           powerActionLabel(config, id));
  snprintf(items[1].search, sizeof(items[1].search), "confirm yes %s", id);
  snprintf(items[1].action, sizeof(items[1].action), "power_confirm|%.31s", id);
  return nativePopupOpen(popup, items, 2, false, true);
}

static int openWeatherLocations(NativePopup *popup, const PanelConfig *config) {
  PopupItem items[PANEL_WEATHER_LOCATION_MAX] = {0};
  for (size_t i = 0; i < config->weatherLocationCount; i++) {
    snprintf(items[i].label,
             sizeof(items[i].label),
             "%s %s",
             i == config->activeWeatherLocation ? "●" : " ",
             config->weatherLocations[i].label);
    snprintf(items[i].search,
             sizeof(items[i].search),
             "%s",
             config->weatherLocations[i].label);
    snprintf(items[i].action,
             sizeof(items[i].action),
             "weather_location|%s",
             config->weatherLocations[i].id);
  }
  return nativePopupOpen(
      popup, items, config->weatherLocationCount, false, true);
}

static const char *activeWeatherLabel(const PanelConfig *config) {
  if (config->activeWeatherLocation < config->weatherLocationCount &&
      config->weatherLocations[config->activeWeatherLocation].label[0])
    return config->weatherLocations[config->activeWeatherLocation].label;
  return config->location;
}

static void loadWeatherForecast(const PanelConfig *config,
                                WeatherForecast *forecast) {
  memset(forecast, 0, sizeof(*forecast));
  char json[32768] = "";
  if (!config->weatherCache[0])
    return;
  int fd = open(config->weatherCache, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return;
  struct stat cacheStat;
  ssize_t length = read(fd, json, sizeof(json) - 1);
  int status = fstat(fd, &cacheStat);
  int saved = errno;
  close(fd);
  errno = saved;
  if (length < 0 || status) {
    memset(forecast, 0, sizeof(*forecast));
    return;
  }
  json[length] = '\0';
  if (weatherForecastParse(json, forecast)) {
    memset(forecast, 0, sizeof(*forecast));
    return;
  }
  forecast->updatedAtValid = true;
  forecast->updatedAt = cacheStat.st_mtime;
}

static int openWeatherForecast(NativePopup *popup,
                               const NativePanel *panel,
                               const PanelConfig *config) {
  WeatherForecast forecast;
  loadWeatherForecast(config, &forecast);
  int actionX = 0, actionWidth = 1;
  if (!nativePanelActionBounds(
          panel, "weather|forecast", &actionX, &actionWidth)) {
    int panelY = 0, panelWidth = 0, panelHeight = 0;
    nativePanelBounds(panel, &actionX, &panelY, &panelWidth, &panelHeight);
    actionX += panelWidth;
  }
  return nativePopupOpenForecast(
      popup, &forecast, activeWeatherLabel(config), actionX, actionWidth);
}

static void updateOpenWeatherForecast(NativePopup *popup,
                                      const PanelConfig *config) {
  if (!nativePopupIsForecastOpen(popup))
    return;
  WeatherForecast forecast;
  loadWeatherForecast(config, &forecast);
  nativePopupUpdateForecast(popup, &forecast, activeWeatherLabel(config));
}

static int openAgenda(NativePopup *popup,
                      const NativePanel *panel,
                      const PanelConfig *config,
                      const AgendaSnapshot *snapshot) {
  AgendaView agenda;
  agendaBuildView(snapshot,
                  time(NULL),
                  config->agendaDays,
                  config->agendaMaxItems,
                  config->agendaMaxUndatedTasks,
                  panelLanguageIsGerman(config),
                  &agenda);
  if (!agenda.available)
    return -1;
  int actionX = 0, actionWidth = 1;
  if (!nativePanelActionBounds(
          panel, "agenda|toggle", &actionX, &actionWidth)) {
    int panelY = 0, panelWidth = 0, panelHeight = 0;
    nativePanelBounds(panel, &actionX, &panelY, &panelWidth, &panelHeight);
    actionX += panelWidth;
  }
  return nativePopupOpenAgenda(popup, &agenda, actionX, actionWidth);
}

static void updateOpenAgenda(NativePopup *popup,
                             const PanelConfig *config,
                             const AgendaSnapshot *snapshot) {
  if (!nativePopupIsAgendaOpen(popup))
    return;
  AgendaView agenda;
  agendaBuildView(snapshot,
                  time(NULL),
                  config->agendaDays,
                  config->agendaMaxItems,
                  config->agendaMaxUndatedTasks,
                  panelLanguageIsGerman(config),
                  &agenda);
  nativePopupUpdateAgenda(popup, &agenda);
}

static void
restartWeather(PanelConfig *config, PanelState *state, pid_t *weatherPid) {
  if (*weatherPid > 0) {
    kill(*weatherPid, SIGTERM);
    waitpid(*weatherPid, NULL, 0);
  }
  moduleWeather(config, state);
  *weatherPid = startWeatherRefresh(config);
}

static void notifyInhibitorState(const PanelConfig *config, bool active) {
  if (!commandExists("notify-send"))
    return;
  bool german = panelLanguageIsGerman(config);
  char *arguments[] = {
      "notify-send",
      active
          ? (german ? "Standby-Sperre aktiviert" : "Sleep inhibition enabled")
          : (german ? "Standby-Sperre deaktiviert"
                    : "Sleep inhibition disabled"),
      active ? (german ? "Automatischer Standby und Ruhezustand "
                         "sind blockiert, bis die Kaffeetasse "
                         "deaktiviert oder Sliverbar beendet wird."
                       : "Automatic standby and hibernation are "
                         "blocked until the coffee-cup inhibitor "
                         "is disabled or Sliverbar exits.")
             : (german ? "Automatischer Standby und Ruhezustand "
                         "sind wieder erlaubt."
                       : "Automatic standby and hibernation are "
                         "allowed again."),
      NULL};
  spawnDetached(arguments);
}

static void stopTimerSound(pid_t pid) {
  if (pid <= 0)
    return;
  kill(pid, SIGTERM);
  for (int i = 0; i < 20 && waitpid(pid, NULL, WNOHANG) == 0; i++)
    usleep(10000);
  if (waitpid(pid, NULL, WNOHANG) == 0) {
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
  }
}

static pid_t playTimerSound(const PanelConfig *config) {
  if (!config->timerSound[0] || access(config->timerSound, R_OK)) {
    logMessage(
        "WARNING", "timer sound is not readable: %s", config->timerSound);
    return -1;
  }
  const char *backend = timerSoundBackend();
  if (!backend) {
    logMessage("WARNING", "no timer sound playback backend is available");
    return -1;
  }
  char *arguments[] = {
      (char *)backend,
      !strcmp(backend, "canberra-gtk-play") ? (char *)"--file"
                                            : (char *)config->timerSound,
      !strcmp(backend, "canberra-gtk-play") ? (char *)config->timerSound : NULL,
      NULL};
  pid_t pid = spawnTracked(arguments);
  if (pid < 0)
    logMessage("WARNING", "cannot start timer sound backend %s", backend);
  return pid;
}

static void notifyTimerStarted(const PanelConfig *config, unsigned minutes) {
  if (!commandExists("notify-send"))
    return;
  bool german = panelLanguageIsGerman(config);
  char duration[64];
  snprintf(duration,
           sizeof(duration),
           german ? "%u Minute%s" : "%u minute%s",
           minutes,
           minutes == 1 ? "" : (german ? "n" : "s"));
  char *arguments[] = {"notify-send",
                       german ? "Timer gestartet" : "Timer started",
                       duration,
                       NULL};
  spawnDetached(arguments);
}

static pid_t notifyTimerExpired(const PanelConfig *config) {
  if (commandExists("notify-send")) {
    bool german = panelLanguageIsGerman(config);
    char *arguments[] = {"notify-send",
                         german ? "Timer abgelaufen" : "Timer elapsed",
                         german ? "Die eingestellte Zeit ist abgelaufen."
                                : "The configured time has elapsed.",
                         NULL};
    spawnDetached(arguments);
  }
  return playTimerSound(config);
}

static void beginTimerExpiry(const PanelConfig *config,
                             Timer *timer,
                             int feedbackTimerFd,
                             pid_t *soundPid) {
  timerShowExpired(timer);
  if (*soundPid > 0)
    kill(*soundPid, SIGTERM);
  *soundPid = notifyTimerExpired(config);
  if (timerFeedbackTimeoutSet(feedbackTimerFd, *soundPid <= 0))
    logMessage("WARNING", "cannot schedule timer feedback timeout");
}

static void
renderTimer(const PanelConfig *config, PanelState *state, const Timer *timer) {
  uint64_t nowNs = timerNowNs();
  moduleTimer(config,
              state,
              timerMinutes(timer),
              timerDisplay(timer),
              timerAnimationFrame(timer, nowNs));
}

static void doAction(PanelConfig *c,
                     PanelState *s,
                     const char *line,
                     bool *volumeDirty,
                     int brightnessTimerFd,
                     int timerFeedbackTimerFd,
                     int timerAnimationTimerFd,
                     WorkspaceBackend *workspaceBackend,
                     NativePanel *panel,
                     NativePopup *popup,
                     pid_t *weatherPid,
                     Inhibitor *inhibitor,
                     Timer *timer,
                     pid_t *timerSoundPid,
                     const AgendaSnapshot *agendaSnapshot) {
  char copybuf[1024];
  snprintf(copybuf, sizeof(copybuf), "%s", line);
  char *nl = strpbrk(copybuf, "\r\n");
  if (nl)
    *nl = '\0';
  char *save = NULL, *kind = strtok_r(copybuf, "|", &save),
       *arg = strtok_r(NULL, "|", &save);
  if (!kind)
    return;
  if (getenv("SLIVERBAR_DEBUG"))
    logMessage("DEBUG", "action=%s arg=%s", kind, arg ? arg : "");
  if (!strcmp(kind, "volume") && arg) {
    if (setVolume(c, arg))
      logMessage("ERROR", "volume action %s failed", arg);
    *volumeDirty = true;
  } else if (!strcmp(kind, "workspace") && arg) {
    workspaceBackendSwitch(workspaceBackend, arg);
  } else if (!strcmp(kind, "inhibitor") && arg && !strcmp(arg, "toggle")) {
    if (!inhibitorToggle(inhibitor)) {
      bool active = inhibitorActive(inhibitor);
      moduleInhibitor(c, s, inhibitorAvailable(inhibitor), active);
      notifyInhibitorState(c, active);
    }
  } else if (!strcmp(kind, "timer") && arg) {
    bool adjusted = false;
    bool animationScheduleChanged = false;
    if (!strcmp(arg, "up"))
      adjusted = timerAdjust(timer, 1);
    else if (!strcmp(arg, "down"))
      adjusted = timerAdjust(timer, -1);
    else if (!strcmp(arg, "reset")) {
      if (timerResetWithFeedback(timer)) {
        animationScheduleChanged = true;
        if (timerFeedbackTimeoutSet(timerFeedbackTimerFd, true))
          logMessage("WARNING", "cannot schedule timer reset feedback");
      }
    } else if (!strcmp(arg, "toggle")) {
      unsigned minutes = timerMinutes(timer);
      TimerTransition transition = timerToggle(timer, timerNowNs());
      animationScheduleChanged = transition != TIMER_TRANSITION_NONE;
      if (transition == TIMER_TRANSITION_STARTED)
        notifyTimerStarted(c, minutes);
      else if (transition == TIMER_TRANSITION_EXPIRED)
        beginTimerExpiry(c, timer, timerFeedbackTimerFd, timerSoundPid);
    }
    if (adjusted && timerFeedbackTimeoutSet(timerFeedbackTimerFd, false))
      logMessage("WARNING", "cannot cancel timer feedback timeout");
    if (animationScheduleChanged &&
        timerAnimationTimeoutSet(timerAnimationTimerFd,
                                 c->iconFont[0] &&
                                     timer->status == TIMER_RUNNING))
      logMessage("WARNING", "cannot update timer animation schedule");
    renderTimer(c, s, timer);
  } else if (!strcmp(kind, "terminal") && arg) {
    char *av[] = {arg, NULL};
    appLaunchTerminal(c, av);
  } else if (!strcmp(kind, "role") && arg) {
    int launchResult = -1;
    if (!strcmp(arg, "system_monitor"))
      launchResult = appLaunchRole(c, APP_ROLE_SYSTEM_MONITOR);
    else if (!strcmp(arg, "network_settings"))
      launchResult = appLaunchRole(c, APP_ROLE_NETWORK_SETTINGS);
    else if (!strcmp(arg, "volume_settings"))
      launchResult = appLaunchRole(c, APP_ROLE_VOLUME_SETTINGS);
    else if (!strcmp(arg, "calendar"))
      launchResult = appLaunchRole(c, APP_ROLE_CALENDAR);
    else if (!strcmp(arg, "tasks"))
      launchResult = appLaunchRole(c, APP_ROLE_TASKS);
    if (!launchResult && nativePopupIsAgendaOpen(popup))
      nativePopupClose(popup);
  } else if (!strcmp(kind, "app") && arg) {
    char spec[320];
    snprintf(spec, sizeof(spec), "desktop:%s", arg);
    appLaunchSpec(c, spec);
  } else if (!strcmp(kind, "power_action") && arg) {
    if (!powerActionAllowed(c->powerActions, arg))
      return;
    if (!powerActionAllowed(c->powerConfirm, arg))
      powerActionExecute(arg);
    else
      openPowerConfirmation(
          popup, c, arg, sleepPowerAction(arg) && inhibitorActive(inhibitor));
  } else if (!strcmp(kind, "power_confirm") && arg) {
    if (!powerActionAllowed(c->powerActions, arg) ||
        !powerActionAllowed(c->powerConfirm, arg))
      return;
    bool restoreInhibitor = sleepPowerAction(arg) && inhibitorActive(inhibitor);
    if (restoreInhibitor)
      inhibitorSetActive(inhibitor, false);
    if (powerActionExecute(arg)) {
      if (restoreInhibitor)
        inhibitorSetActive(inhibitor, true);
      if (commandExists("notify-send")) {
        char *arguments[] = {
            "notify-send", "Sliverbar", "Power action failed", NULL};
        spawnDetached(arguments);
      }
    }
    moduleInhibitor(
        c, s, inhibitorAvailable(inhibitor), inhibitorActive(inhibitor));
  } else if (!strcmp(kind, "power_cancel")) {
    nativePopupClose(popup);
  } else if (!strcmp(kind, "power_profile") && arg) {
    if (!strcmp(arg, "menu")) {
      if (nativePopupIsOpen(popup))
        nativePopupClose(popup);
      else if (!powerProfilesQuery(c, &(PowerProfileState){0}))
        openPowerProfiles(popup, panel, c);
    } else {
      char error[256];
      if (powerProfileSet(arg, error, sizeof(error))) {
        logMessage("ERROR", "cannot set power profile %s: %s", arg, error);
        if (commandExists("notify-send")) {
          char *arguments[] = {
              "notify-send", "Sliverbar", "Power profile change failed", NULL};
          spawnDetached(arguments);
        }
      }
      PowerProfileState profileState;
      c->internalPowerProfilesAvailable = !powerProfilesQuery(c, &profileState);
      moduleBattery(c, s);
      nativePopupClose(popup);
    }
  } else if (!strcmp(kind, "weather_location") && arg) {
    if (selectWeatherLocation(c, arg)) {
      if (c->weatherState[0])
        writeAtomic(c->weatherState, arg, 0600);
      restartWeather(c, s, weatherPid);
    }
  } else if (!strcmp(kind, "launcher")) {
    if (nativePopupIsOpen(popup))
      nativePopupClose(popup);
    else if (c->internalLauncherAvailable &&
             strcmp(c->applicationLauncher, "external") != 0)
      openApplicationLauncher(popup);
    else if (*c->launcher)
      appLaunchSpec(c, c->launcher);
  } else if (!strcmp(kind, "power")) {
    if (nativePopupIsOpen(popup))
      nativePopupClose(popup);
    else if (c->internalPowerAvailable &&
             strcmp(c->powerMenuMode, "external") != 0)
      openPowerMenu(popup, c);
    else if (*c->powerMenu)
      appLaunchSpec(c, c->powerMenu);
  } else if (!strcmp(kind, "notify") && arg && commandExists("notify-send")) {
    char *message = strtok_r(NULL, "", &save);
    char *av[] = {"notify-send", arg, message ? message : "", NULL};
    spawnDetached(av);
  } else if (!strcmp(kind, "weather") && arg && !strcmp(arg, "forecast")) {
    if (nativePopupIsForecastOpen(popup))
      nativePopupClose(popup);
    else
      openWeatherForecast(popup, panel, c);
  } else if (!strcmp(kind, "weather") && arg && !strcmp(arg, "locations")) {
    if (nativePopupIsOpen(popup))
      nativePopupClose(popup);
    else
      openWeatherLocations(popup, c);
  } else if (!strcmp(kind, "weather") && arg && !strcmp(arg, "refresh")) {
    restartWeather(c, s, weatherPid);
  } else if (!strcmp(kind, "agenda") && arg && !strcmp(arg, "toggle")) {
    if (nativePopupIsAgendaOpen(popup))
      nativePopupClose(popup);
    else if (c->internalAgendaAvailable)
      openAgenda(popup, panel, c, agendaSnapshot);
  } else if (!strcmp(kind, "brightness") && arg) {
    if (scheduleBrightness(c, s, arg, brightnessTimerFd))
      logMessage(
          "ERROR", "cannot schedule brightness update: %s", strerror(errno));
  } else if (!strcmp(kind, "refresh") && arg) {
    if (!strcmp(arg, "volume"))
      moduleVolume(c, s);
    else if (!strcmp(arg, "brightness") && !s->brightnessUpdatePending)
      moduleBrightness(c, s);
  }
}
#endif

static void usage(FILE *f, const char *name) {
  fprintf(f,
          "Usage: %s [--config PATH] [--check-config] [--diagnose] "
          "[--list-pim-sources] [--smoke-test] [--version]\n"
          "       %s --action {volume {up|down|toggle}|"
          "brightness {up|down}|refresh {volume|brightness}}\n",
          name,
          name);
}

static int runDiagnostics(const PanelConfig *config,
                          const char *configPath,
                          const char *executable) {
  AgendaProviderStatus agendaStatus = {0};
  if (!strcmp(config->agendaProvider, "eds") && agendaProviderEdsCompiled()) {
    AgendaProvider *provider = agendaProviderCreate(config);
    if (provider && !agendaProviderStart(provider)) {
      struct pollfd agendaPoll = {agendaProviderPollFd(provider), POLLIN, 0};
      if (poll(&agendaPoll, 1, 11000) > 0) {
        AgendaSnapshot snapshot;
        agendaProviderRead(provider, &snapshot, &agendaStatus);
      }
    }
    agendaProviderDestroy(provider);
  }
  printf("version=%s\n", SLIVERBAR_VERSION);
  printf("config=%s\n", configPath ? configPath : "internal-defaults");
  printf("display=%s\n", getenv("DISPLAY") ? getenv("DISPLAY") : "unavailable");
  printf("font=%s\n", config->font);
  printf("icon_font=%s\n",
         config->iconFont[0] ? config->iconFont : "system-fallback");
  printf("gio=%s\n", appLauncherHasGio() ? "yes" : "no");
  printf("agenda.provider=%s\n", config->agendaProvider);
  printf("agenda.eds_compiled=%s\n",
         agendaProviderEdsCompiled() ? "yes" : "no");
#ifdef HAVE_NATIVE_PANEL
  printf("agenda.popup=compiled\n");
#else
  printf("agenda.popup=unavailable\n");
#endif
  printf("agenda.sources.selected=%zu\n", agendaStatus.selectedSources);
  printf("agenda.sources.reachable=%zu\n", agendaStatus.reachableSources);
  printf("agenda.sources.failed=%zu\n", agendaStatus.failedSources);
  printf("launcher.mode=%s\n", config->applicationLauncher);
  const size_t DIAGNOSTIC_APP_CAPACITY = 512;
  AppEntry *catalog = calloc(DIAGNOSTIC_APP_CAPACITY, sizeof(*catalog));
  size_t catalogCount =
      catalog ? appCatalogLoad(catalog, DIAGNOSTIC_APP_CAPACITY) : 0;
  free(catalog);
  printf("launcher.applications=%zu\n", catalogCount);
  printf("launcher.external=%s\n",
         config->launcher[0]
             ? (appSpecAvailable(config, config->launcher) ? config->launcher
                                                           : "unavailable")
             : "unconfigured");
#ifdef HAVE_NATIVE_PANEL
  printf("native_x11=yes\n");
#ifdef HAVE_XKBCOMMON_X11
  printf("xkbcommon_x11=yes\n");
#else
  printf("xkbcommon_x11=no\n");
#endif
  int screenNumber = 0;
  xcb_connection_t *connection = xcb_connect(NULL, &screenNumber);
  if (!xcb_connection_has_error(connection)) {
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screenNumber && screens.rem; i++)
      xcb_screen_next(&screens);
    if (screens.rem) {
      WorkspaceBackend *backend =
          workspaceBackendCreate(connection, screens.data->root, config);
      printf("workspace_backend=%s\n", workspaceBackendName(backend));
      workspaceBackendDestroy(backend);
    } else {
      printf("workspace_backend=unavailable\n");
    }
  } else {
    printf("workspace_backend=unavailable\n");
  }
  xcb_disconnect(connection);
#else
  printf("native_x11=no\n");
  printf("xkbcommon_x11=no\n");
  printf("workspace_backend=unavailable\n");
#endif
  static const char *const PROGRAMS[] = {"pactl",
                                         "wpctl",
                                         "amixer",
                                         "nmcli",
                                         "xrandr",
                                         "curl",
                                         "notify-send",
                                         "xdg-open",
                                         "systemd-inhibit",
                                         "xdg-terminal-exec"};
  for (size_t i = 0; i < sizeof(PROGRAMS) / sizeof(PROGRAMS[0]); i++)
    printf("program.%s=%s\n",
           PROGRAMS[i],
           commandExists(PROGRAMS[i]) ? "yes" : "no");
  WifiDiagnostic wifi;
  if (!wifiDiagnostic(&wifi)) {
    printf("wifi.interface=%s\n", wifi.interface);
    printf("wifi.backend=%s\n", wifi.backend);
    printf("wifi.raw=%.1f\n", wifi.rawValue);
    printf("wifi.percent=%d\n", wifi.percent);
  } else {
    printf("wifi.interface=unavailable\n");
    printf("wifi.backend=unavailable\n");
    printf("wifi.raw=unavailable\n");
    printf("wifi.percent=unavailable\n");
  }
  char description[512];
  appDescribeTerminal(config, description, sizeof(description));
  printf("terminal=%s\n", description);
  printf("language.mode=%s\n", config->language);
  printf("language=%s\n", panelLanguage(config));
  printf("timer.sound=%s\n",
         config->timerSound[0] ? config->timerSound : "disabled");
  printf("timer.audio_backend=%s\n",
         timerSoundBackend() ? timerSoundBackend() : "unavailable");
  for (AppRole role = APP_ROLE_SYSTEM_MONITOR; role <= APP_ROLE_TASKS; role++) {
    if ((role == APP_ROLE_CALENDAR && strcmp(config->calendar, "auto") != 0) ||
        (role == APP_ROLE_TASKS && strcmp(config->tasks, "auto") != 0))
      snprintf(description,
               sizeof(description),
               "%s",
               appRoleAvailable(config, role) ? "configured" : "unavailable");
    else
      appDescribeRole(config, role, description, sizeof(description));
    printf("role.%s=%s\n", appRoleName(role), description);
  }
  printf("weather.locations=%zu\n", config->weatherLocationCount);
  if (config->weatherLocationCount)
    printf("weather.active=%s\n",
           config->weatherLocations[config->activeWeatherLocation].id);
  PowerAction powerActions[16];
  size_t powerActionCount =
      powerActionList(config,
                      config->powerActions,
                      powerActions,
                      sizeof(powerActions) / sizeof(powerActions[0]));
  printf("power.backend=%s\n",
         powerActionCount ? "logind-dbus" : "unavailable");
  printf("power.actions=%zu\n", powerActionCount);
  for (size_t i = 0; i < powerActionCount; i++)
    printf("power.action.%s=%s\n",
           powerActions[i].id,
           powerActions[i].authorization);
  PowerProfileState profileState;
  powerProfilesQuery(config, &profileState);
  printf("power_profiles.backend=%s\n",
         powerProfilesBackendName(&profileState));
  printf("power_profiles.active=%s\n",
         profileState.active[0] ? profileState.active : "unavailable");
  printf("power_profiles.profiles=%zu\n", profileState.count);
  for (size_t i = 0; i < profileState.count; i++)
    printf("power_profiles.profile.%zu=%s\n", i, profileState.profiles[i].id);
  Inhibitor *inhibitor = inhibitorCreate(executable);
  printf("inhibitor.backend=%s\n", inhibitorBackendName(inhibitor));
  printf("inhibitor.active=no\n");
  inhibitorDestroy(inhibitor);
  return 0;
}

#ifdef HAVE_NATIVE_PANEL
static int smokeTestNativeTray(NativePanel *panel,
                               xcb_connection_t *connection,
                               xcb_screen_t *screen) {
  if (!nativePanelOwnsTray(panel)) {
    logMessage("ERROR",
               "native smoke-test could not own the system tray selection");
    return -1;
  }
  xcb_window_t icon = xcb_generate_id(connection);
  uint32_t values[] = {screen->black_pixel, XCB_EVENT_MASK_STRUCTURE_NOTIFY};
  xcb_create_window(connection,
                    screen->root_depth,
                    icon,
                    screen->root,
                    0,
                    0,
                    16,
                    16,
                    0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                    values);
  xcb_client_message_event_t dock = {0};
  dock.response_type = XCB_CLIENT_MESSAGE;
  dock.format = 32;
  dock.window = nativePanelWindow(panel);
  dock.type = nativePanelTrayOpcode(panel);
  dock.data.data32[1] = 0;
  dock.data.data32[2] = icon;
  char action[16] = "";
  bool redraw = false;
  nativePanelHandleEvent(panel,
                         (const xcb_generic_event_t *)&dock,
                         action,
                         sizeof(action),
                         &redraw);
  xcb_query_tree_reply_t *tree =
      xcb_query_tree_reply(connection, xcb_query_tree(connection, icon), NULL);
  xcb_window_t container = tree ? tree->parent : XCB_WINDOW_NONE;
  free(tree);
  tree = container == XCB_WINDOW_NONE
             ? NULL
             : xcb_query_tree_reply(
                   connection, xcb_query_tree(connection, container), NULL);
  xcb_window_t host = tree ? tree->parent : XCB_WINDOW_NONE;
  free(tree);
  tree = host == XCB_WINDOW_NONE
             ? NULL
             : xcb_query_tree_reply(
                   connection, xcb_query_tree(connection, host), NULL);
  bool embedded = tree && tree->parent == screen->root &&
                  nativePanelTrayIconCount(panel) == 1 && redraw;
  free(tree);
  if (!embedded) {
    logMessage("ERROR", "native smoke-test tray docking failed");
    xcb_destroy_window(connection, icon);
    return -1;
  }
  xcb_atom_t windowType = atom(connection, "_NET_WM_WINDOW_TYPE");
  xcb_atom_t dockType = atom(connection, "_NET_WM_WINDOW_TYPE_DOCK");
  xcb_get_property_reply_t *type = xcb_get_property_reply(
      connection,
      xcb_get_property(connection, 0, host, windowType, XCB_ATOM_ATOM, 0, 1),
      NULL);
  bool hostIsDock =
      type && xcb_get_property_value_length(type) == (int)sizeof(xcb_atom_t) &&
      *(const xcb_atom_t *)xcb_get_property_value(type) == dockType;
  free(type);
  if (!hostIsDock) {
    logMessage("ERROR", "native smoke-test tray host is not a dock window");
    xcb_destroy_window(connection, icon);
    return -1;
  }
  PanelState state = {0};
  if (nativePanelDraw(panel, &state)) {
    logMessage("ERROR", "native smoke-test tray layout failed");
    xcb_destroy_window(connection, icon);
    return -1;
  }
  xcb_get_input_focus_reply_t *sync = xcb_get_input_focus_reply(
      connection, xcb_get_input_focus(connection), NULL);
  free(sync);
  bool configured = false;
  xcb_generic_event_t *event;
  while ((event = xcb_poll_for_event(connection))) {
    if ((event->response_type & 0x7fU) == XCB_CONFIGURE_NOTIFY &&
        (event->response_type & 0x80U) != 0 &&
        ((xcb_configure_notify_event_t *)event)->window == icon)
      configured = true;
    free(event);
  }
  if (!configured) {
    logMessage("ERROR", "native smoke-test tray configure notification failed");
    xcb_destroy_window(connection, icon);
    return -1;
  }
  xcb_destroy_notify_event_t destroyed = {0};
  destroyed.response_type = XCB_DESTROY_NOTIFY;
  destroyed.window = icon;
  nativePanelHandleEvent(panel,
                         (const xcb_generic_event_t *)&destroyed,
                         action,
                         sizeof(action),
                         &redraw);
  xcb_destroy_window(connection, icon);
  xcb_flush(connection);
  if (nativePanelTrayIconCount(panel) != 0) {
    logMessage("ERROR", "native smoke-test tray cleanup failed");
    return -1;
  }
  return 0;
}
#endif

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");
  if (argc == 2 && !strcmp(argv[1], "--inhibit-holder")) {
    for (;;)
      pause();
  }
  PanelConfig cfg;
  configDefaults(&cfg);
  const char *config = NULL;
  const char *actionModule = NULL, *actionOperation = NULL;
  char defaultConfig[PANEL_PATH_MAX];
  bool check = false, diagnose = false, listPimSources = false,
       smokeTest = false;
  signal(SIGPIPE, SIG_IGN);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--config") && i + 1 < argc)
      config = argv[++i];
    else if (!strcmp(argv[i], "--check-config"))
      check = true;
    else if (!strcmp(argv[i], "--diagnose"))
      diagnose = true;
    else if (!strcmp(argv[i], "--list-pim-sources"))
      listPimSources = true;
    else if (!strcmp(argv[i], "--smoke-test"))
      smokeTest = true;
    else if (!strcmp(argv[i], "--action") && i + 2 < argc) {
      actionModule = argv[++i];
      actionOperation = argv[++i];
    } else if (!strcmp(argv[i], "--version")) {
      puts("sliverbar " SLIVERBAR_VERSION);
      return 0;
    } else {
      usage(stderr, argv[0]);
      return 2;
    }
  }
  if (actionModule) {
    char controlAction[CONTROL_ACTION_MAX], controlPath[PANEL_PATH_MAX];
    if (argc != 4 || !controlActionBuild(actionModule,
                                         actionOperation,
                                         controlAction,
                                         sizeof(controlAction))) {
      usage(stderr, argv[0]);
      return 2;
    }
    if (controlSocketPath(controlPath, sizeof(controlPath)) ||
        controlClientSend(controlPath, controlAction)) {
      logMessage("ERROR", "cannot send panel action: %s", strerror(errno));
      return 1;
    }
    return 0;
  }
  if (!config) {
    config = getenv("SLIVERBAR_CONFIG");
    if (!config) {
      const char *configHome = getenv("XDG_CONFIG_HOME");
      const char *home = getenv("HOME");
      bool userConfigFound = false;
      if (configHome) {
        if (!joinPath(defaultConfig,
                      sizeof(defaultConfig),
                      configHome,
                      "/sliverbar/panel.conf") &&
            access(defaultConfig, R_OK) == 0)
          userConfigFound = true;
      } else if (home &&
                 !joinPath(defaultConfig,
                           sizeof(defaultConfig),
                           home,
                           "/.config/sliverbar/panel.conf") &&
                 access(defaultConfig, R_OK) == 0) {
        userConfigFound = true;
      }
      if (userConfigFound)
        config = defaultConfig;
      else if (access(SLIVERBAR_SYSTEM_CONFIG, R_OK) == 0)
        config = SLIVERBAR_SYSTEM_CONFIG;
      else if (access("panel.conf", R_OK) == 0)
        config = "panel.conf";
      else if (access("config/panel.conf", R_OK) == 0)
        config = "config/panel.conf";
    }
  }
  char error[512];
  if (config && configLoad(&cfg, config, error, sizeof(error))) {
    logMessage("ERROR", "%s", error);
    return 1;
  }
#ifdef HAVE_NATIVE_PANEL
  const char *home = getenv("HOME");
  const char *cache = getenv("XDG_CACHE_HOME");
  const char *stateHome = getenv("XDG_STATE_HOME");
  char cacheDefault[PANEL_PATH_MAX], stateDefault[PANEL_PATH_MAX];
  if (!cache && home &&
      !joinPath(cacheDefault, sizeof(cacheDefault), home, "/.cache"))
    cache = cacheDefault;
  if (!stateHome && home &&
      !joinPath(stateDefault, sizeof(stateDefault), home, "/.local/state"))
    stateHome = stateDefault;
  if (stateHome && cfg.weatherLocationCount)
    joinPath(cfg.weatherState,
             sizeof(cfg.weatherState),
             stateHome,
             "/sliverbar/weather-location");
  char savedWeatherLocation[64];
  if (cfg.weatherState[0] && !readTextFile(cfg.weatherState,
                                           savedWeatherLocation,
                                           sizeof(savedWeatherLocation)))
    selectWeatherLocation(&cfg, savedWeatherLocation);
  if (cache && cfg.location[0] && !cfg.weatherCache[0]) {
    joinPath(cfg.weatherCacheRoot,
             sizeof(cfg.weatherCacheRoot),
             cache,
             "/sliverbar/weather");
    updateWeatherPaths(&cfg);
  }
#endif
  if (check) {
    puts("configuration valid");
    return 0;
  }
  if (listPimSources)
    return agendaProviderListSources(stdout, stderr);
  if (diagnose)
    return runDiagnostics(&cfg, config, argv[0]);
#ifdef HAVE_NATIVE_PANEL
  if (cfg.weatherState[0]) {
    char stateDirectory[PANEL_PATH_MAX];
    snprintf(stateDirectory, sizeof(stateDirectory), "%s", cfg.weatherState);
    char *slash = strrchr(stateDirectory, '/');
    if (slash) {
      *slash = '\0';
      mkdirP(stateDirectory, 0700);
    }
  }
#endif
#ifndef HAVE_NATIVE_PANEL
  (void)smokeTest;
  logMessage("ERROR",
             "native X11 support was unavailable when this binary was built");
  return 1;
#else
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  sigaddset(&signals, SIGCHLD);
  if (sigprocmask(SIG_BLOCK, &signals, NULL)) {
    logMessage("ERROR", "cannot block runtime signals: %s", strerror(errno));
    return 1;
  }
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  char fallback[64];
  if (!runtime) {
    snprintf(fallback, sizeof(fallback), "/tmp/sliverbar-%ld", (long)getuid());
    runtime = fallback;
  }
  char dir[PANEL_PATH_MAX], lockpath[PANEL_PATH_MAX];
  if (joinPath(dir, sizeof(dir), runtime, "/sliverbar") ||
      joinPath(lockpath, sizeof(lockpath), dir, "/panel.lock")) {
    logMessage("ERROR", "runtime path is too long");
    return 1;
  }
  if (mkdirP(dir, 0700)) {
    perror(dir);
    return 1;
  }
  int lock = open(lockpath, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
    logMessage("ERROR", "another instance is running");
    return 200;
  }
  ftruncate(lock, 0);
  dprintf(lock, "%ld\n", (long)getpid());
  int xfd = -1;
  int screenNumber = 0;
  xcb_connection_t *x = xcb_connect(NULL, &screenNumber);
  if (xcb_connection_has_error(x)) {
    logMessage("ERROR", "cannot connect to X11");
    return 1;
  }
  const xcb_setup_t *setup = xcb_get_setup(x);
  xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < screenNumber && it.rem; i++)
    xcb_screen_next(&it);
  if (!it.rem) {
    logMessage("ERROR", "X11 selected an unavailable screen");
    xcb_disconnect(x);
    return 1;
  }
  xcb_screen_t *screen = it.data;
  xcb_window_t root = screen->root;
  snprintf(cfg.geometry,
           sizeof(cfg.geometry),
           "%ux%d+0+0",
           screen->width_in_pixels,
           cfg.height);
  xcb_atom_t active = atom(x, "_NET_ACTIVE_WINDOW"),
             utf8 = atom(x, "UTF8_STRING"), netname = atom(x, "_NET_WM_NAME");
  uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
  xcb_change_window_attributes(x, root, XCB_CW_EVENT_MASK, &mask);
  xcb_flush(x);
  xfd = xcb_get_file_descriptor(x);
  NativePanel *panel = nativePanelCreate(x, screen, &cfg, error, sizeof(error));
  if (!panel) {
    logMessage("ERROR", "%s", error);
    xcb_disconnect(x);
    return 1;
  }
  if (smokeTest) {
    PanelState smoke = {0};
    xcb_window_t titleWindow = xcb_generate_id(x);
    xcb_create_window(x,
                      XCB_COPY_FROM_PARENT,
                      titleWindow,
                      root,
                      0,
                      0,
                      1,
                      1,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual,
                      0,
                      NULL);
    const char SYNTHETIC_TITLE[] = "Synthetic title";
    xcb_change_property(x,
                        XCB_PROP_MODE_REPLACE,
                        titleWindow,
                        XCB_ATOM_WM_NAME,
                        utf8,
                        8,
                        sizeof(SYNTHETIC_TITLE) - 1,
                        SYNTHETIC_TITLE);
    const char SYNTHETIC_CLASS[] = "synthetic\0SyntheticApp\0";
    xcb_change_property(x,
                        XCB_PROP_MODE_REPLACE,
                        titleWindow,
                        XCB_ATOM_WM_CLASS,
                        XCB_ATOM_STRING,
                        8,
                        sizeof(SYNTHETIC_CLASS) - 1,
                        SYNTHETIC_CLASS);
    xcb_change_property(x,
                        XCB_PROP_MODE_REPLACE,
                        root,
                        active,
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        &titleWindow);
    updateTitleXcb(x, root, active, utf8, netname, cfg.titleMax, &smoke, &cfg);
    if (!strstr(smoke.title, SYNTHETIC_TITLE)) {
      logMessage("ERROR", "active-window title lookup failed");
      xcb_destroy_window(x, titleWindow);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_delete_property(x, titleWindow, XCB_ATOM_WM_NAME);
    updateTitleXcb(x, root, active, utf8, netname, cfg.titleMax, &smoke, &cfg);
    if (!strstr(smoke.title, "SyntheticApp")) {
      logMessage("ERROR", "window class title fallback failed");
      xcb_destroy_window(x, titleWindow);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_window_t noActiveWindow = XCB_WINDOW_NONE;
    smoke.focusedWorkspaceKnown = true;
    smoke.focusedWorkspaceOccupied = true;
    xcb_change_property(x,
                        XCB_PROP_MODE_REPLACE,
                        root,
                        active,
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        &noActiveWindow);
    updateTitleXcb(x, root, active, utf8, netname, cfg.titleMax, &smoke, &cfg);
    if (!strstr(smoke.title, "SyntheticApp")) {
      logMessage("ERROR",
                 "transient missing active window replaced the current title");
      xcb_destroy_window(x, titleWindow);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    smoke.focusedWorkspaceOccupied = false;
    updateTitleXcb(x, root, active, utf8, netname, cfg.titleMax, &smoke, &cfg);
    if (!strstr(smoke.title, "Desktop")) {
      logMessage("ERROR", "empty workspace did not display the desktop title");
      xcb_destroy_window(x, titleWindow);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    uint32_t desktopCount = 3, currentDesktop = 1, windowDesktop = 1;
    xcb_atom_t numberOfDesktops = atom(x, "_NET_NUMBER_OF_DESKTOPS");
    xcb_atom_t currentDesktopAtom = atom(x, "_NET_CURRENT_DESKTOP");
    xcb_atom_t desktopNames = atom(x, "_NET_DESKTOP_NAMES");
    xcb_atom_t clientList = atom(x, "_NET_CLIENT_LIST");
    xcb_atom_t windowDesktopAtom = atom(x, "_NET_WM_DESKTOP");
    const char DESKTOP_NAMES[] = "web\0code\0chat\0";
    xcb_change_property(x,
                        XCB_PROP_MODE_REPLACE,
                        root,
                        numberOfDesktops,
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        &desktopCount);
    xcb_change_property(x,
                        XCB_PROP_MODE_REPLACE,
                        root,
                        currentDesktopAtom,
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        &currentDesktop);
    xcb_change_property(x,
                        XCB_PROP_MODE_REPLACE,
                        root,
                        desktopNames,
                        utf8,
                        8,
                        sizeof(DESKTOP_NAMES) - 1,
                        DESKTOP_NAMES);
    xcb_change_property(x,
                        XCB_PROP_MODE_REPLACE,
                        root,
                        clientList,
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        &titleWindow);
    xcb_change_property(x,
                        XCB_PROP_MODE_REPLACE,
                        titleWindow,
                        windowDesktopAtom,
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        &windowDesktop);
    WorkspaceBackend *smokeWorkspace = workspaceBackendCreate(x, root, &cfg);
    bool workspaceOk = smokeWorkspace != NULL;
    if (workspaceOk && !strcmp(cfg.workspaceBackend, "none")) {
      workspaceBackendRefresh(smokeWorkspace, &smoke);
      workspaceOk = !strcmp(workspaceBackendName(smokeWorkspace), "none") &&
                    !smoke.workspace[0] && !smoke.focusedWorkspaceKnown;
    } else if (workspaceOk && !strcmp(cfg.workspaceBackend, "bspwm")) {
      workspaceOk = !strcmp(workspaceBackendName(smokeWorkspace), "bspwm");
    } else if (workspaceOk) {
      workspaceOk = workspaceBackendRefresh(smokeWorkspace, &smoke) &&
                    !strcmp(workspaceBackendName(smokeWorkspace), "ewmh") &&
                    smoke.focusedWorkspaceKnown &&
                    smoke.focusedWorkspaceOccupied &&
                    strstr(smoke.workspace, "workspace|1") &&
                    !workspaceBackendSwitch(smokeWorkspace, "2");
    }
    if (!workspaceOk) {
      logMessage("ERROR", "EWMH workspace backend smoke-test failed");
      workspaceBackendDestroy(smokeWorkspace);
      xcb_destroy_window(x, titleWindow);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    workspaceBackendDestroy(smokeWorkspace);
    xcb_destroy_window(x, titleWindow);
    snprintf(smoke.workspace,
             sizeof(smoke.workspace),
             "%%{F%s}%%{B%s}%%{A3:notify|Native panel|space preserved:}"
             "%%{A4:volume|up:}%%{A1:workspace|I:}%%{O6}native%%{O6}"
             "%%{A}%%{A}%%{A}%%{B-}%%{F-}",
             cfg.colorFocus,
             cfg.colorBg);
    snprintf(smoke.title,
             sizeof(smoke.title),
             "%%{B%s}%%{F%s}%%{A1:smoke|title:} Native X11 panel with a "
             "deliberately long centered title that must never overlap either "
             "aligned side %%{A}%%{F-}%%{B-}",
             cfg.colorBg,
             cfg.colorFree);
    snprintf(smoke.clock,
             sizeof(smoke.clock),
             "%%{B%s}%%{F%s}%%{A1:smoke|right:} smoke test %%{A}%%{F-}%%{B-}",
             cfg.colorBg,
             cfg.colorClock);
    if (nativePanelDraw(panel, &smoke)) {
      logMessage("ERROR", "native smoke-test rendering failed");
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    const char *expectedActions[] = {
        "workspace|I", "notify|Native panel|space preserved", "volume|up"};
    const uint8_t BUTTONS[] = {1, 3, 4};
    for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); i++) {
      xcb_button_press_event_t click = {0};
      click.response_type = XCB_BUTTON_PRESS;
      click.event = nativePanelWindow(panel);
      click.event_x = 4;
      click.detail = BUTTONS[i];
      char smokeAction[64] = "";
      bool redraw = false;
      if (!nativePanelHandleEvent(panel,
                                  (const xcb_generic_event_t *)&click,
                                  smokeAction,
                                  sizeof(smokeAction),
                                  &redraw) ||
          strcmp(smokeAction, expectedActions[i]) != 0) {
        logMessage("ERROR", "native smoke-test action routing failed");
        nativePanelDestroy(panel);
        xcb_disconnect(x);
        close(lock);
        return 1;
      }
    }
    int smokeX = 0, smokeY = 0, smokeWidth = 0, smokeHeight = 0;
    nativePanelBounds(panel, &smokeX, &smokeY, &smokeWidth, &smokeHeight);
    int actionX = 0, actionWidth = 0;
    if (!nativePanelActionBounds(panel,
                                 "notify|Native panel|space preserved",
                                 &actionX,
                                 &actionWidth) ||
        actionX != smokeX || actionWidth <= 12) {
      logMessage("ERROR", "native smoke-test action bounds failed");
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    int leftX = 0, leftWidth = 0, titleX = 0, titleWidth = 0, rightX = 0;
    if (!nativePanelActionBounds(
            panel, "notify|Native panel|space preserved", &leftX, &leftWidth) ||
        !nativePanelActionBounds(panel, "smoke|title", &titleX, &titleWidth) ||
        !nativePanelActionBounds(panel, "smoke|right", &rightX, NULL) ||
        titleWidth <= 0 || titleX < leftX + leftWidth + 8 ||
        titleX + titleWidth > rightX - 8) {
      logMessage("ERROR", "native smoke-test aligned content overlap");
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    if (smokeTestNativeTray(panel, x, screen)) {
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    NativePopup *smokePopup = nativePopupCreate(x, screen, &cfg);
    nativePopupSetBounds(smokePopup, smokeX, smokeY, smokeWidth, smokeHeight);
    PopupItem popupItems[2] = {
        {.label = "First", .search = "first", .action = "popup|first"},
        {.label = "Second", .search = "second", .action = "popup|second"},
    };
    xcb_set_input_focus(x,
                        XCB_INPUT_FOCUS_POINTER_ROOT,
                        nativePanelWindow(panel),
                        XCB_CURRENT_TIME);
    xcb_flush(x);
    if (!nativePopupAvailable(smokePopup) ||
        nativePopupOpen(smokePopup, popupItems, 2, false, false)) {
      logMessage("ERROR", "native popup smoke-test could not open");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_get_input_focus_reply_t *popupFocus =
        xcb_get_input_focus_reply(x, xcb_get_input_focus(x), NULL);
    xcb_connection_t *inputProbe = xcb_connect(NULL, NULL);
    xcb_grab_keyboard_reply_t *keyboardProbe = NULL;
    xcb_grab_pointer_reply_t *pointerProbe = NULL;
    if (popupFocus && popupFocus->focus != nativePanelWindow(panel) &&
        inputProbe && !xcb_connection_has_error(inputProbe)) {
      keyboardProbe =
          xcb_grab_keyboard_reply(inputProbe,
                                  xcb_grab_keyboard(inputProbe,
                                                    0,
                                                    screen->root,
                                                    XCB_CURRENT_TIME,
                                                    XCB_GRAB_MODE_ASYNC,
                                                    XCB_GRAB_MODE_ASYNC),
                                  NULL);
      pointerProbe =
          xcb_grab_pointer_reply(inputProbe,
                                 xcb_grab_pointer(inputProbe,
                                                  0,
                                                  screen->root,
                                                  XCB_EVENT_MASK_BUTTON_PRESS,
                                                  XCB_GRAB_MODE_ASYNC,
                                                  XCB_GRAB_MODE_ASYNC,
                                                  XCB_WINDOW_NONE,
                                                  XCB_CURSOR_NONE,
                                                  XCB_CURRENT_TIME),
                                 NULL);
    }
    bool inputAvailable = keyboardProbe && pointerProbe &&
                          keyboardProbe->status == XCB_GRAB_STATUS_SUCCESS &&
                          pointerProbe->status == XCB_GRAB_STATUS_SUCCESS;
    if (inputProbe && !xcb_connection_has_error(inputProbe)) {
      xcb_ungrab_keyboard(inputProbe, XCB_CURRENT_TIME);
      xcb_ungrab_pointer(inputProbe, XCB_CURRENT_TIME);
      xcb_flush(inputProbe);
    }
    free(pointerProbe);
    free(keyboardProbe);
    if (inputProbe)
      xcb_disconnect(inputProbe);
    free(popupFocus);
    if (!inputAvailable) {
      logMessage("ERROR", "native popup monopolized global input");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_button_press_event_t popupClick = {0};
    popupClick.response_type = XCB_BUTTON_PRESS;
    popupClick.event = screen->root;
    popupClick.detail = 1;
    popupClick.root_x = (int16_t)(smokeX + 5);
    popupClick.root_y = (int16_t)(smokeY + cfg.height + 5);
    char popupAction[64] = "";
    bool popupRedraw = false;
    if (!nativePopupHandleEvent(smokePopup,
                                (xcb_generic_event_t *)&popupClick,
                                popupAction,
                                sizeof(popupAction),
                                &popupRedraw) ||
        strcmp(popupAction, "popup|first") != 0 ||
        nativePopupIsOpen(smokePopup)) {
      logMessage("ERROR", "native popup smoke-test routing failed");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_get_input_focus_reply_t *restoredFocus =
        xcb_get_input_focus_reply(x, xcb_get_input_focus(x), NULL);
    bool focusRestored =
        restoredFocus && restoredFocus->focus == nativePanelWindow(panel);
    free(restoredFocus);
    if (!focusRestored) {
      logMessage("ERROR", "native popup did not restore input focus");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    if (nativePopupOpenAt(
            smokePopup, popupItems, 2, false, rightX, actionWidth)) {
      logMessage("ERROR", "anchored native popup could not open");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    int anchoredX = 0, anchoredWidth = 0;
    nativePopupGeometry(smokePopup, &anchoredX, NULL, &anchoredWidth, NULL);
    if (anchoredX < smokeX || anchoredX + anchoredWidth > smokeX + smokeWidth) {
      logMessage("ERROR", "anchored native popup exceeded panel bounds");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    nativePopupClose(smokePopup);
    if (nativePopupOpen(smokePopup, popupItems, 2, false, false)) {
      logMessage("ERROR", "native popup smoke-test could not reopen");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_focus_out_event_t staleFocusOut = {0};
    staleFocusOut.response_type = XCB_FOCUS_OUT;
    if (!nativePopupHandleEvent(smokePopup,
                                (xcb_generic_event_t *)&staleFocusOut,
                                popupAction,
                                sizeof(popupAction),
                                &popupRedraw) ||
        !nativePopupIsOpen(smokePopup)) {
      logMessage("ERROR", "stale focus event closed a reopened popup");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_set_input_focus(x,
                        XCB_INPUT_FOCUS_POINTER_ROOT,
                        nativePanelWindow(panel),
                        XCB_CURRENT_TIME);
    if (!nativePopupHandleEvent(smokePopup,
                                (xcb_generic_event_t *)&staleFocusOut,
                                popupAction,
                                sizeof(popupAction),
                                &popupRedraw) ||
        nativePopupIsOpen(smokePopup)) {
      logMessage("ERROR", "real focus loss did not close native popup");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    WeatherForecast emptyForecast = {0};
    if (nativePopupOpenForecast(smokePopup,
                                &emptyForecast,
                                "Forecast smoke test",
                                smokeX + smokeWidth - 80,
                                80) ||
        !nativePopupIsForecastOpen(smokePopup)) {
      logMessage("ERROR", "native forecast popup could not open");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    int forecastX = 0, forecastY = 0, forecastWidth = 0, emptyHeight = 0;
    nativePopupGeometry(
        smokePopup, &forecastX, &forecastY, &forecastWidth, &emptyHeight);
    WeatherForecast smokeForecast = {
        .dayCount = WEATHER_FORECAST_DAY_COUNT,
        .updatedAtValid = true,
        .updatedAt = time(NULL),
    };
    for (size_t day = 0; day < WEATHER_FORECAST_DAY_COUNT; day++) {
      WeatherForecastDay *forecastDay = &smokeForecast.days[day];
      forecastDay->available = true;
      snprintf(forecastDay->date,
               sizeof(forecastDay->date),
               "2026-07-%02zu",
               23 + day);
      forecastDay->minimumValid = true;
      forecastDay->minimumC = 7 + (int)day;
      forecastDay->maximumValid = true;
      forecastDay->maximumC = 16 + (int)day;
      for (size_t slot = 0; slot < WEATHER_FORECAST_SLOT_COUNT; slot++) {
        WeatherForecastSlot *forecastSlot = &forecastDay->slots[slot];
        forecastSlot->hour = 6 + (int)slot * 3;
        forecastSlot->temperatureValid = true;
        forecastSlot->temperatureC = 8 + (int)day + (int)slot;
        forecastSlot->rainValid = true;
        forecastSlot->rainPercent = (int)slot * 10;
        forecastSlot->codeValid = true;
        forecastSlot->weatherCode = slot % 2 == 0 ? 113 : 176;
        forecastSlot->condition =
            weatherConditionFromCode(forecastSlot->weatherCode);
      }
    }
    nativePopupUpdateForecast(
        smokePopup, &smokeForecast, "Updated forecast smoke test");
    int forecastHeight = 0;
    nativePopupGeometry(smokePopup, NULL, NULL, NULL, &forecastHeight);
    if (forecastX < smokeX || forecastX + forecastWidth > smokeX + smokeWidth ||
        forecastY != smokeY + smokeHeight || emptyHeight >= forecastHeight) {
      logMessage("ERROR", "native forecast popup geometry failed");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_button_press_event_t forecastClick = {0};
    forecastClick.response_type = XCB_BUTTON_PRESS;
    forecastClick.event = screen->root;
    forecastClick.detail = 1;
    forecastClick.root_x = (int16_t)(forecastX + 5);
    forecastClick.root_y = (int16_t)(forecastY + 5);
    popupAction[0] = '\0';
    if (!nativePopupHandleEvent(smokePopup,
                                (xcb_generic_event_t *)&forecastClick,
                                popupAction,
                                sizeof(popupAction),
                                &popupRedraw) ||
        popupAction[0] || !nativePopupIsForecastOpen(smokePopup)) {
      logMessage("ERROR", "native forecast popup was interactive");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    forecastClick.root_y = (int16_t)(smokeY + 1);
    if (!nativePopupHandleEvent(smokePopup,
                                (xcb_generic_event_t *)&forecastClick,
                                popupAction,
                                sizeof(popupAction),
                                &popupRedraw) ||
        nativePopupIsOpen(smokePopup)) {
      logMessage("ERROR", "native forecast popup did not close outside");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    AgendaView smokeAgenda = {.count = 2,
                              .hiddenEvents = 1,
                              .hiddenTasks = 2,
                              .initialized = true,
                              .available = true};
    smokeAgenda.items[0].item.type = AGENDA_ITEM_EVENT;
    snprintf(smokeAgenda.items[0].item.title,
             sizeof(smokeAgenda.items[0].item.title),
             "%s",
             "Calendar smoke item with a deliberately long title that wraps "
             "instead of being truncated at the popup edge");
    snprintf(smokeAgenda.items[0].item.organizer,
             sizeof(smokeAgenda.items[0].item.organizer),
             "%s",
             "Max Mustermann");
    snprintf(smokeAgenda.items[0].item.sourceName,
             sizeof(smokeAgenda.items[0].item.sourceName),
             "%s",
             "Shared calendar");
    snprintf(smokeAgenda.items[0].when,
             sizeof(smokeAgenda.items[0].when),
             "%s",
             "Heute 12:00");
    smokeAgenda.items[1].item.type = AGENDA_ITEM_TASK;
    snprintf(smokeAgenda.items[1].item.title,
             sizeof(smokeAgenda.items[1].item.title),
             "%s",
             "Task smoke item");
    snprintf(smokeAgenda.items[1].when,
             sizeof(smokeAgenda.items[1].when),
             "%s",
             "Morgen");
    if (nativePopupOpenAgenda(
            smokePopup, &smokeAgenda, smokeX + smokeWidth - 100, 100) ||
        !nativePopupIsAgendaOpen(smokePopup)) {
      logMessage("ERROR", "native agenda popup could not open");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    int agendaX = 0, agendaY = 0, agendaWidth = 0, agendaHeight = 0;
    nativePopupGeometry(
        smokePopup, &agendaX, &agendaY, &agendaWidth, &agendaHeight);
    xcb_get_input_focus_reply_t *agendaFocus =
        xcb_get_input_focus_reply(x, xcb_get_input_focus(x), NULL);
    bool agendaKeptFocus =
        agendaFocus && agendaFocus->focus == nativePanelWindow(panel);
    free(agendaFocus);
    int minimumAgendaHeight = 4 * (cfg.height > 28 ? cfg.height : 28);
    if (!agendaKeptFocus || agendaY != smokeY + smokeHeight ||
        agendaX < smokeX || agendaX + agendaWidth > smokeX + smokeWidth ||
        agendaHeight <= minimumAgendaHeight) {
      logMessage("ERROR", "native agenda focus or geometry failed");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_button_press_event_t agendaClick = {0};
    agendaClick.response_type = XCB_BUTTON_PRESS;
    agendaClick.event = screen->root;
    agendaClick.detail = 1;
    agendaClick.root_x = (int16_t)(agendaX + 5);
    agendaClick.root_y = (int16_t)(agendaY + 5);
    popupAction[0] = '\0';
    if (!nativePopupHandleEvent(smokePopup,
                                (xcb_generic_event_t *)&agendaClick,
                                popupAction,
                                sizeof(popupAction),
                                &popupRedraw) ||
        strcmp(popupAction, "role|calendar") != 0 ||
        !nativePopupIsAgendaOpen(smokePopup)) {
      logMessage("ERROR", "native agenda row routing failed");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    agendaClick.root_y = (int16_t)(smokeY + 1);
    if (!nativePopupHandleEvent(smokePopup,
                                (xcb_generic_event_t *)&agendaClick,
                                popupAction,
                                sizeof(popupAction),
                                &popupRedraw) ||
        nativePopupIsOpen(smokePopup)) {
      logMessage("ERROR", "native agenda did not close outside");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_connection_t *agendaProbe = xcb_connect(NULL, NULL);
    xcb_grab_pointer_reply_t *agendaPointer =
        agendaProbe && !xcb_connection_has_error(agendaProbe)
            ? xcb_grab_pointer_reply(
                  agendaProbe,
                  xcb_grab_pointer(agendaProbe,
                                   0,
                                   screen->root,
                                   XCB_EVENT_MASK_BUTTON_PRESS,
                                   XCB_GRAB_MODE_ASYNC,
                                   XCB_GRAB_MODE_ASYNC,
                                   XCB_WINDOW_NONE,
                                   XCB_CURSOR_NONE,
                                   XCB_CURRENT_TIME),
                  NULL)
            : NULL;
    bool agendaReleasedPointer =
        agendaPointer && agendaPointer->status == XCB_GRAB_STATUS_SUCCESS;
    if (agendaReleasedPointer)
      xcb_ungrab_pointer(agendaProbe, XCB_CURRENT_TIME);
    free(agendaPointer);
    if (agendaProbe)
      xcb_disconnect(agendaProbe);
    if (!agendaReleasedPointer) {
      logMessage("ERROR", "native agenda did not release pointer grab");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    if (nativePopupOpenForecast(smokePopup,
                                &smokeForecast,
                                "Escape forecast smoke test",
                                smokeX + smokeWidth - 80,
                                80)) {
      logMessage("ERROR", "native forecast popup could not reopen");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    xcb_keycode_t escape = keycodeForKeysym(x, 0xff1bU);
    xcb_key_press_event_t escapePress = {0};
    escapePress.response_type = XCB_KEY_PRESS;
    escapePress.event = screen->root;
    escapePress.detail = escape;
    if (!escape ||
        !nativePopupHandleEvent(smokePopup,
                                (xcb_generic_event_t *)&escapePress,
                                popupAction,
                                sizeof(popupAction),
                                &popupRedraw) ||
        nativePopupIsOpen(smokePopup)) {
      logMessage("ERROR", "Escape did not close native forecast popup");
      nativePopupDestroy(smokePopup);
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    nativePopupDestroy(smokePopup);
    usleep(100000);
    nativePanelDestroy(panel);
    xcb_disconnect(x);
    close(lock);
    return 0;
  }
  NativePopup *popup = nativePopupCreate(x, screen, &cfg);
  int popupX = 0, popupY = 0, popupWidth = 0, popupPanelHeight = 0;
  nativePanelBounds(panel, &popupX, &popupY, &popupWidth, &popupPanelHeight);
  nativePopupSetBounds(popup, popupX, popupY, popupWidth, popupPanelHeight);
  Inhibitor *inhibitor = inhibitorCreate(argv[0]);
  cfg.internalLauncherAvailable =
      nativePopupAvailable(popup) && appLauncherHasGio();
  PowerAction availablePowerActions[16];
  cfg.internalPowerAvailable =
      nativePopupAvailable(popup) &&
      powerActionList(&cfg,
                      cfg.powerActions,
                      availablePowerActions,
                      sizeof(availablePowerActions) /
                          sizeof(availablePowerActions[0])) > 0;
  PowerProfileState initialProfileState;
  cfg.internalPowerProfilesAvailable =
      nativePopupAvailable(popup) &&
      !powerProfilesQuery(&cfg, &initialProfileState);
  cfg.internalWeatherForecastAvailable = nativePopupAvailable(popup);
  AgendaSnapshot agendaSnapshot = {0};
  AgendaProviderStatus agendaStatus = {0};
  bool agendaStatusLogged = false;
  AgendaProvider *agendaProvider = NULL;
  if (!strcmp(cfg.agendaProvider, "eds")) {
    if (!agendaProviderEdsCompiled()) {
      logMessage("WARNING",
                 "agenda is unavailable: EDS support is not compiled in");
    } else {
      agendaProvider = agendaProviderCreate(&cfg);
      if (!agendaProvider || agendaProviderStart(agendaProvider)) {
        logMessage("WARNING", "agenda provider could not be started");
        agendaProviderDestroy(agendaProvider);
        agendaProvider = NULL;
      }
    }
  }
  int sfd = signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK);
  int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  int brightnessTfd =
      timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  int timerFeedbackTfd =
      timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  int timerAnimationTfd =
      timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (tfd < 0 || brightnessTfd < 0 || timerFeedbackTfd < 0 ||
      timerAnimationTfd < 0) {
    logMessage("ERROR", "cannot create runtime timer: %s", strerror(errno));
    if (tfd >= 0)
      close(tfd);
    if (brightnessTfd >= 0)
      close(brightnessTfd);
    if (timerFeedbackTfd >= 0)
      close(timerFeedbackTfd);
    if (timerAnimationTfd >= 0)
      close(timerAnimationTfd);
    agendaProviderDestroy(agendaProvider);
    inhibitorDestroy(inhibitor);
    nativePopupDestroy(popup);
    nativePanelDestroy(panel);
    xcb_disconnect(x);
    close(lock);
    return 1;
  }
  struct itimerspec tick = {{1, 0}, {0, 1}};
  timerfd_settime(tfd, 0, &tick, NULL);
  WorkspaceBackend *workspaceBackend = workspaceBackendCreate(x, root, &cfg);
  if (!workspaceBackend) {
    agendaProviderDestroy(agendaProvider);
    inhibitorDestroy(inhibitor);
    nativePopupDestroy(popup);
    nativePanelDestroy(panel);
    xcb_disconnect(x);
    close(tfd);
    close(brightnessTfd);
    close(timerFeedbackTfd);
    close(timerAnimationTfd);
    close(sfd);
    close(lock);
    return 1;
  }
  Child networkEvents = {.readFd = -1, .writeFd = -1};
  if (cfg.moduleNetwork != MODULE_DISABLED && commandExists("nmcli")) {
    char *nm[] = {"nmcli", "monitor", NULL};
    if (childPipe(nm, false, &networkEvents))
      logMessage("ERROR", "cannot start nmcli monitor");
  }
  Child titleRoot = {.readFd = -1, .writeFd = -1};
  Child titleWindow = {.readFd = -1, .writeFd = -1};
#ifndef HAVE_XCB
  if (start_active_window_watcher(&title_root))
    log_message("ERROR", "cannot start active-window watcher");
  if (start_window_title_watcher(&title_window))
    log_message("ERROR", "cannot start window-title watcher");
#endif
  PanelState state = {0};
  Timer timer = {0};
  pid_t timerSoundPid = 0;
  pid_t weatherPid = startWeatherRefresh(&cfg);
  moduleStatic(&cfg, &state);
  moduleClock(&cfg, &state);
  moduleCpu(&cfg, &state);
  moduleBattery(&cfg, &state);
  moduleScreencast(&cfg, &state, runtime);
  moduleVolume(&cfg, &state);
  moduleNetwork(&cfg, &state);
  moduleBrightness(&cfg, &state);
  moduleWeather(&cfg, &state);
  renderTimer(&cfg, &state, &timer);
  moduleInhibitor(
      &cfg, &state, inhibitorAvailable(inhibitor), inhibitorActive(inhibitor));
  workspaceBackendRefresh(workspaceBackend, &state);
  updateTitleXcb(x, root, active, utf8, netname, cfg.titleMax, &state, &cfg);
  char action[1024] = "";
  char controlPath[PANEL_PATH_MAX] = "";
  int controlFd = -1;
  if (controlSocketPath(controlPath, sizeof(controlPath))) {
    logMessage(
        "WARNING", "control socket path is unavailable: %s", strerror(errno));
  } else {
    controlFd = controlServerOpen(controlPath);
    if (controlFd < 0)
      logMessage("WARNING", "cannot open control socket: %s", strerror(errno));
  }
  unsigned ticks = 0;
  bool running = true, dirty = true, vd = false;
  while (running) {
    struct pollfd fds[] = {
        {tfd, POLLIN, 0},
        {sfd, POLLIN, 0},
        {workspaceBackendPollFd(workspaceBackend), POLLIN, 0},
        {brightnessTfd, POLLIN, 0},
        {xfd, POLLIN, 0},
        {networkEvents.readFd, POLLIN, 0},
        {titleRoot.readFd, POLLIN, 0},
        {titleWindow.readFd, POLLIN, 0},
        {timerFeedbackTfd, POLLIN, 0},
        {timerAnimationTfd, POLLIN, 0},
        {agendaProviderPollFd(agendaProvider), POLLIN, 0},
        {controlFd, POLLIN, 0}};
    if (poll(fds, 12, -1) < 0) {
      if (errno == EINTR)
        continue;
      logMessage("ERROR", "poll failed: %s", strerror(errno));
      break;
    }
    if (fds[0].revents & POLLIN) {
      uint64_t n;
      read(tfd, &n, sizeof(n));
      ticks += (unsigned)n;
      moduleClock(&cfg, &state);
      updateOpenAgenda(popup, &cfg, &agendaSnapshot);
      workspaceBackendRefresh(workspaceBackend, &state);
      updateTitleXcb(
          x, root, active, utf8, netname, cfg.titleMax, &state, &cfg);
      moduleScreencast(&cfg, &state, runtime);
      if (timerUpdate(&timer, timerNowNs())) {
        if (timerAnimationTimeoutSet(timerAnimationTfd, false))
          logMessage("WARNING", "cannot stop timer animation");
        beginTimerExpiry(&cfg, &timer, timerFeedbackTfd, &timerSoundPid);
      }
      renderTimer(&cfg, &state, &timer);
      if (ticks % 5 == 0)
        moduleCpu(&cfg, &state);
      if (ticks % 5 == 0)
        moduleVolume(&cfg, &state);
      if (ticks % 10 == 0) {
        PowerProfileState currentProfileState;
        cfg.internalPowerProfilesAvailable =
            nativePopupAvailable(popup) &&
            !powerProfilesQuery(&cfg, &currentProfileState);
        moduleBattery(&cfg, &state);
      }
      if (ticks % cfg.networkInterval == 0)
        moduleNetwork(&cfg, &state);
      if (ticks % cfg.weatherInterval == 0 && weatherPid <= 0)
        weatherPid = startWeatherRefresh(&cfg);
      if (cfg.moduleNetwork != MODULE_DISABLED && networkEvents.pid <= 0 &&
          commandExists("nmcli")) {
        char *nm[] = {"nmcli", "monitor", NULL};
        if (childPipe(nm, false, &networkEvents))
          logMessage("ERROR", "cannot restart nmcli monitor");
      }
#ifndef HAVE_XCB
      if (title_root.pid <= 0) {
        if (start_active_window_watcher(&title_root))
          log_message("ERROR", "cannot restart active-window watcher");
      }
      if (title_window.pid <= 0 && start_window_title_watcher(&title_window))
        log_message("ERROR", "cannot restart window-title watcher");
#endif
      dirty = true;
    }
    if (fds[1].revents & POLLIN) {
      struct signalfd_siginfo si;
      while (read(sfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
        if (si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM)
          running = false;
        else if (si.ssi_signo == SIGCHLD) {
          pid_t reaped;
          int status;
          while ((reaped = waitpid(-1, &status, WNOHANG)) > 0) {
            if (reaped == weatherPid) {
              weatherPid = 0;
              moduleWeather(&cfg, &state);
              updateOpenWeatherForecast(popup, &cfg);
              dirty = true;
            } else if (workspaceBackendChildExited(
                           workspaceBackend, reaped, status, &state)) {
              dirty = true;
            } else if (inhibitorChildExited(inhibitor, reaped)) {
              moduleInhibitor(
                  &cfg, &state, inhibitorAvailable(inhibitor), false);
              dirty = true;
            } else if (reaped == timerSoundPid) {
              timerSoundPid = 0;
              bool succeeded = WIFEXITED(status) && WEXITSTATUS(status) == 0;
              TimerFeedbackAction feedback =
                  timerSoundFinished(&timer, succeeded);
              if (feedback != TIMER_FEEDBACK_NONE) {
                if (feedback == TIMER_FEEDBACK_TIMEOUT) {
                  if (timerFeedbackTimeoutSet(timerFeedbackTfd, true))
                    logMessage("WARNING",
                               "cannot schedule timer feedback timeout");
                } else {
                  timerClearFeedback(&timer);
                  if (timerFeedbackTimeoutSet(timerFeedbackTfd, false))
                    logMessage("WARNING",
                               "cannot cancel timer feedback timeout");
                }
                renderTimer(&cfg, &state, &timer);
                dirty = true;
              }
            } else if (reaped == networkEvents.pid) {
              networkEvents.pid = 0;
              if (networkEvents.readFd >= 0)
                close(networkEvents.readFd);
              networkEvents.readFd = -1;
            } else if (reaped == titleRoot.pid) {
              titleRoot.pid = 0;
              if (titleRoot.readFd >= 0)
                close(titleRoot.readFd);
              titleRoot.readFd = -1;
            } else if (reaped == titleWindow.pid) {
              titleWindow.pid = 0;
              if (titleWindow.readFd >= 0)
                close(titleWindow.readFd);
              titleWindow.readFd = -1;
            }
          }
        }
      }
    }
    if (fds[2].revents & (POLLIN | POLLHUP)) {
      if (workspaceBackendRead(workspaceBackend, &state)) {
        updateTitleXcb(
            x, root, active, utf8, netname, cfg.titleMax, &state, &cfg);
        dirty = true;
      }
    }
    if (fds[4].revents & POLLIN) {
      xcb_generic_event_t *ev;
      while ((ev = xcb_poll_for_event(x))) {
        bool redraw = false;
        action[0] = '\0';
        if (nativePopupHandleEvent(
                popup, ev, action, sizeof(action), &redraw)) {
          if (action[0])
            doAction(&cfg,
                     &state,
                     action,
                     &vd,
                     brightnessTfd,
                     timerFeedbackTfd,
                     timerAnimationTfd,
                     workspaceBackend,
                     panel,
                     popup,
                     &weatherPid,
                     inhibitor,
                     &timer,
                     &timerSoundPid,
                     &agendaSnapshot);
          free(ev);
          continue;
        }
        if (nativePanelHandleEvent(
                panel, ev, action, sizeof(action), &redraw)) {
          doAction(&cfg,
                   &state,
                   action,
                   &vd,
                   brightnessTfd,
                   timerFeedbackTfd,
                   timerAnimationTfd,
                   workspaceBackend,
                   panel,
                   popup,
                   &weatherPid,
                   inhibitor,
                   &timer,
                   &timerSoundPid,
                   &agendaSnapshot);
          dirty = true;
        }
        if ((ev->response_type & 0x7fU) == XCB_UNMAP_NOTIFY &&
            ((xcb_unmap_notify_event_t *)ev)->window ==
                nativePanelWindow(panel))
          nativePopupClose(popup);
        int nextPopupX = 0, nextPopupY = 0, nextPopupWidth = 0,
            nextPopupPanelHeight = 0;
        nativePanelBounds(panel,
                          &nextPopupX,
                          &nextPopupY,
                          &nextPopupWidth,
                          &nextPopupPanelHeight);
        if (nextPopupX != popupX || nextPopupY != popupY ||
            nextPopupWidth != popupWidth ||
            nextPopupPanelHeight != popupPanelHeight) {
          popupX = nextPopupX;
          popupY = nextPopupY;
          popupWidth = nextPopupWidth;
          popupPanelHeight = nextPopupPanelHeight;
          nativePopupSetBounds(
              popup, popupX, popupY, popupWidth, popupPanelHeight);
        }
        if ((ev->response_type & 0x7fU) == XCB_PROPERTY_NOTIFY) {
          workspaceBackendRefresh(workspaceBackend, &state);
          updateTitleXcb(
              x, root, active, utf8, netname, cfg.titleMax, &state, &cfg);
          dirty = true;
        }
        if (redraw)
          dirty = true;
        free(ev);
      }
    }
    if (fds[3].revents & POLLIN) {
      uint64_t expirations;
      if (read(brightnessTfd, &expirations, sizeof(expirations)) ==
          (ssize_t)sizeof(expirations)) {
        state.brightnessUpdatePending = false;
        if (applyBrightness(&state)) {
          logMessage("ERROR", "brightness update failed");
          moduleBrightness(&cfg, &state);
          dirty = true;
        }
      }
    }
    if (fds[5].revents & POLLIN) {
      char discard[2048];
      while (read(networkEvents.readFd, discard, sizeof(discard)) > 0) {
      }
      moduleNetwork(&cfg, &state);
      dirty = true;
    }
    if (fds[8].revents & POLLIN) {
      uint64_t expirations;
      if (read(timerFeedbackTfd, &expirations, sizeof(expirations)) ==
          (ssize_t)sizeof(expirations)) {
        timerClearFeedback(&timer);
        renderTimer(&cfg, &state, &timer);
        dirty = true;
      }
    }
    if (fds[9].revents & POLLIN) {
      uint64_t expirations;
      if (read(timerAnimationTfd, &expirations, sizeof(expirations)) ==
          (ssize_t)sizeof(expirations)) {
        renderTimer(&cfg, &state, &timer);
        dirty = true;
      }
    }
    if (fds[10].revents & POLLIN) {
      bool wasAvailable = cfg.internalAgendaAvailable;
      if (agendaProviderRead(agendaProvider, &agendaSnapshot, &agendaStatus)) {
        cfg.internalAgendaAvailable = nativePopupAvailable(popup) &&
                                      agendaStatus.initialized &&
                                      agendaStatus.reachableSources > 0;
        if (!agendaStatusLogged ||
            cfg.internalAgendaAvailable != wasAvailable) {
          logMessage(cfg.internalAgendaAvailable ? "INFO" : "WARNING",
                     "agenda is %s (%zu reachable, %zu failed sources)",
                     cfg.internalAgendaAvailable ? "available" : "unavailable",
                     agendaStatus.reachableSources,
                     agendaStatus.failedSources);
          agendaStatusLogged = true;
        }
        moduleClock(&cfg, &state);
        updateOpenAgenda(popup, &cfg, &agendaSnapshot);
        dirty = true;
      }
    }
    if (fds[11].revents & POLLIN) {
      char controlAction[CONTROL_ACTION_MAX];
      while (controlServerReceive(
                 controlFd, controlAction, sizeof(controlAction)) > 0) {
        if (!controlActionValid(controlAction)) {
          logMessage("WARNING", "ignored invalid control action");
          continue;
        }
        doAction(&cfg,
                 &state,
                 controlAction,
                 &vd,
                 brightnessTfd,
                 timerFeedbackTfd,
                 timerAnimationTfd,
                 workspaceBackend,
                 panel,
                 popup,
                 &weatherPid,
                 inhibitor,
                 &timer,
                 &timerSoundPid,
                 &agendaSnapshot);
        dirty = true;
      }
    }
#ifndef HAVE_XCB
    if (fds[6].revents & POLLIN) {
      char event[2048];
      size_t used = 0;
      ssize_t count;
      while (used < sizeof(event) - 1 &&
             (count = read(title_root.read_fd,
                           event + used,
                           sizeof(event) - used - 1)) > 0)
        used += (size_t)count;
      event[used] = '\0';
      retire_child(&title_window);
      char id[32];
      if (active_id_from_event(event, id, sizeof(id)) ||
          start_window_title_watcher_for_id(&title_window, id))
        log_message("ERROR", "cannot follow the active window title");
    }
    if (fds[7].revents & POLLIN) {
      char event[2048];
      size_t used = 0;
      ssize_t count;
      while (used < sizeof(event) - 1 &&
             (count = read(title_window.read_fd,
                           event + used,
                           sizeof(event) - used - 1)) > 0)
        used += (size_t)count;
      event[used] = '\0';
      title_from_event(event, cfg.title_max, &state, &cfg);
      dirty = true;
    }
#endif
    if (vd) {
      moduleVolume(&cfg, &state);
      vd = false;
      dirty = true;
    }
    if (dirty) {
      if (nativePanelDraw(panel, &state)) {
        logMessage("ERROR", "native panel rendering failed");
        running = false;
      }
      dirty = false;
    }
    if (xcb_connection_has_error(x)) {
      logMessage("ERROR", "X11 connection failed");
      running = false;
    }
  }
  close(tfd);
  close(brightnessTfd);
  close(timerFeedbackTfd);
  close(timerAnimationTfd);
  close(sfd);
  controlServerClose(controlFd, controlPath[0] ? controlPath : NULL);
  stopTimerSound(timerSoundPid);
  if (weatherPid > 0) {
    kill(weatherPid, SIGTERM);
    waitpid(weatherPid, NULL, 0);
  }
  stopChild(&networkEvents);
  stopChild(&titleWindow);
  stopChild(&titleRoot);
  agendaProviderDestroy(agendaProvider);
  workspaceBackendDestroy(workspaceBackend);
  inhibitorDestroy(inhibitor);
  nativePopupDestroy(popup);
  nativePanelDestroy(panel);
  xcb_disconnect(x);
  close(lock);
  return 0;
#endif
}
