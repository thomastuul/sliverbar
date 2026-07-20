#include "native_panel.h"
#include "panel.h"
#include "version.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/signalfd.h>
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
  if (getenv("LEMONBAR_C_DEBUG"))
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
  char step[24], ignored[256];
  snprintf(step, sizeof(step), "%d%%", c->volumeStep);
  if (commandExists("pactl")) {
    char value[32];
    if (!strcmp(op, "up"))
      snprintf(value, sizeof(value), "+%s", step);
    else if (!strcmp(op, "down"))
      snprintf(value, sizeof(value), "-%s", step);
    else {
      char *av[] = {"pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle", NULL};
      return runCapture(av, ignored, sizeof(ignored), 1500);
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

static int setBrightness(const PanelConfig *c, PanelState *s, const char *op) {
  if (!s->brightnessInitialized || !*s->brightnessOutput)
    return -1;
  int target = s->brightnessPercent +
               (!strcmp(op, "up") ? c->brightnessStep : -c->brightnessStep);
  if (target < 5)
    target = 5;
  if (target > 100)
    target = 100;
  char value[32];
  snprintf(value, sizeof(value), "%.2f", (double)target / 100.0);
  char *outv[] = {
      "xrandr", "--output", s->brightnessOutput, "--brightness", value, NULL};
  char ignored[128];
  if (runCapture(outv, ignored, sizeof(ignored), 1500))
    return -1;
  s->brightnessPercent = target;
  moduleBrightnessValue(c, s, target);
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
           c->language);
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

static void refreshWeatherImage(const PanelConfig *c) {
  if (!*c->weatherImage || !commandExists("curl"))
    return;
  char location[256], url[512], tmp[PANEL_PATH_MAX], parent[PANEL_PATH_MAX];
  snprintf(location, sizeof(location), "%s", c->location);
  for (char *p = location; *p; p++)
    if (*p == ' ')
      *p = '+';
  snprintf(url,
           sizeof(url),
           "https://v2.wttr.in/%s.png?lang=%s&m&2",
           location,
           c->language);
  if (joinPath(tmp, sizeof(tmp), c->weatherImage, ".tmp"))
    return;
  snprintf(parent, sizeof(parent), "%s", c->weatherImage);
  char *slash = strrchr(parent, '/');
  if (slash) {
    *slash = '\0';
    mkdirP(parent, 0700);
  }
  char ignored[256];
  char *av[] = {"curl",
                "-fsSL",
                "--connect-timeout",
                "3",
                "--max-time",
                "15",
                "-o",
                tmp,
                url,
                NULL};
  if (!runCapture(av, ignored, sizeof(ignored), 18000))
    rename(tmp, c->weatherImage);
  else
    unlink(tmp);
}

static pid_t startWeatherRefresh(const PanelConfig *c) {
  pid_t pid = fork();
  if (pid == 0) {
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
    refreshWeather(c);
    refreshWeatherImage(c);
    _exit(0);
  }
  return pid;
}

static void doAction(const PanelConfig *c,
                     PanelState *s,
                     const char *line,
                     bool *volumeDirty) {
  char copybuf[1024];
  snprintf(copybuf, sizeof(copybuf), "%s", line);
  char *nl = strpbrk(copybuf, "\r\n");
  if (nl)
    *nl = '\0';
  char *save = NULL, *kind = strtok_r(copybuf, "|", &save),
       *arg = strtok_r(NULL, "|", &save);
  if (!kind)
    return;
  if (getenv("LEMONBAR_C_DEBUG"))
    logMessage("DEBUG", "action=%s arg=%s", kind, arg ? arg : "");
  if (!strcmp(kind, "volume") && arg) {
    setVolume(c, arg);
    *volumeDirty = true;
  } else if (!strcmp(kind, "workspace") && arg) {
    char *av[] = {"bspc", "desktop", "-f", arg, NULL};
    char out[64];
    runCapture(av, out, sizeof(out), 1000);
  } else if (!strcmp(kind, "terminal") && arg) {
    char *av[] = {(char *)c->terminal, "-e", arg, NULL};
    spawnDetached(av);
  } else if (!strcmp(kind, "launcher") && *c->launcher) {
    char *av[] = {(char *)c->launcher, NULL};
    spawnDetached(av);
  } else if (!strcmp(kind, "power") && *c->powerMenu) {
    char *av[] = {(char *)c->powerMenu, NULL};
    spawnDetached(av);
  } else if (!strcmp(kind, "notify") && arg) {
    char *message = strtok_r(NULL, "", &save);
    char *av[] = {"notify-send", arg, message ? message : "", NULL};
    spawnDetached(av);
  } else if (!strcmp(kind, "weather") && arg && !strcmp(arg, "open") &&
             *c->weatherImage) {
    char *av[] = {"xdg-open", (char *)c->weatherImage, NULL};
    spawnDetached(av);
  } else if (!strcmp(kind, "weather") && arg && !strcmp(arg, "notify")) {
    char summary[128];
    snprintf(summary, sizeof(summary), "Weather: %.118s", c->location);
    char *av[] = {"notify-send",
                  summary,
                  "Left-click opens the three-day forecast.",
                  NULL};
    spawnDetached(av);
  } else if (!strcmp(kind, "brightness") && arg) {
    setBrightness(c, s, arg);
  }
}
#endif

static void usage(FILE *f, const char *name) {
  fprintf(
      f,
      "Usage: %s [--config PATH] [--check-config] [--smoke-test] [--version]\n",
      name);
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
  PanelConfig cfg;
  configDefaults(&cfg);
  const char *config = NULL;
  bool check = false, smokeTest = false;
  signal(SIGPIPE, SIG_IGN);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--config") && i + 1 < argc)
      config = argv[++i];
    else if (!strcmp(argv[i], "--check-config"))
      check = true;
    else if (!strcmp(argv[i], "--smoke-test"))
      smokeTest = true;
    else if (!strcmp(argv[i], "--version")) {
      puts("lemonbar-panel " LEMONBAR_C_VERSION);
      return 0;
    } else {
      usage(stderr, argv[0]);
      return 2;
    }
  }
  if (!config) {
    config = getenv("LEMONBAR_C_CONFIG");
    if (!config)
      config = "panel.conf";
  }
  char error[512];
  if (configLoad(&cfg, config, error, sizeof(error))) {
    logMessage("ERROR", "%s", error);
    return 1;
  }
  const char *home = getenv("HOME");
  const char *cache = getenv("XDG_CACHE_HOME");
  char cacheDefault[PANEL_PATH_MAX], suffix[512];
  if (!cache && home &&
      !joinPath(cacheDefault, sizeof(cacheDefault), home, "/.cache"))
    cache = cacheDefault;
  if (home && !*cfg.launcher)
    joinPath(cfg.launcher,
             sizeof(cfg.launcher),
             home,
             "/.config/bspwm/rofi/launcher/launcher.sh");
  if (home && !*cfg.powerMenu)
    joinPath(cfg.powerMenu,
             sizeof(cfg.powerMenu),
             home,
             "/.config/bspwm/rofi/powermenu/powermenu.sh");
  if (cache && !*cfg.weatherCache) {
    snprintf(suffix, sizeof(suffix), "/weather/%s.json", cfg.location);
    joinPath(cfg.weatherCache, sizeof(cfg.weatherCache), cache, suffix);
  }
  if (cache && !*cfg.weatherImage) {
    snprintf(suffix, sizeof(suffix), "/weather/%s_3days.png", cfg.location);
    joinPath(cfg.weatherImage, sizeof(cfg.weatherImage), cache, suffix);
  }
  if (check) {
    puts("configuration valid");
    return 0;
  }
#ifndef HAVE_NATIVE_PANEL
  (void)smokeTest;
  logMessage("ERROR",
             "native X11 support was unavailable when this binary was built");
  return 1;
#else
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  char fallback[64];
  if (!runtime) {
    snprintf(fallback, sizeof(fallback), "/tmp/lemonbar-c-%ld", (long)getuid());
    runtime = fallback;
  }
  char dir[PANEL_PATH_MAX], lockpath[PANEL_PATH_MAX];
  if (joinPath(dir, sizeof(dir), runtime, "/lemonbar-c") ||
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
  xcb_connection_t *x = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(x)) {
    logMessage("ERROR", "cannot connect to X11");
    return 1;
  }
  const xcb_setup_t *setup = xcb_get_setup(x);
  xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
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
    xcb_destroy_window(x, titleWindow);
    snprintf(smoke.workspace,
             sizeof(smoke.workspace),
             "%%{F%s}%%{B%s}%%{A3:notify|Native panel|space preserved:}"
             "%%{A4:volume|up:}%%{A1:workspace|I:} native "
             "%%{A}%%{A}%%{A}%%{B-}%%{F-}",
             cfg.colorFocus,
             cfg.colorBg);
    snprintf(smoke.title,
             sizeof(smoke.title),
             "%%{B%s}%%{F%s} Native X11 panel %%{F-}%%{B-}",
             cfg.colorBg,
             cfg.colorFree);
    snprintf(smoke.clock,
             sizeof(smoke.clock),
             "%%{B%s}%%{F%s} smoke test %%{F-}%%{B-}",
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
    if (smokeTestNativeTray(panel, x, screen)) {
      nativePanelDestroy(panel);
      xcb_disconnect(x);
      close(lock);
      return 1;
    }
    usleep(100000);
    nativePanelDestroy(panel);
    xcb_disconnect(x);
    close(lock);
    return 0;
  }
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  sigaddset(&signals, SIGCHLD);
  sigprocmask(SIG_BLOCK, &signals, NULL);
  int sfd = signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK);
  int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  struct itimerspec tick = {{1, 0}, {0, 1}};
  timerfd_settime(tfd, 0, &tick, NULL);
  char *bspArgv[] = {"bspc", "subscribe", "report", NULL};
  Child bsp = {.readFd = -1, .writeFd = -1};
  if (childPipe(bspArgv, false, &bsp)) {
    nativePanelDestroy(panel);
    xcb_disconnect(x);
    return 1;
  }
  Child networkEvents = {.readFd = -1, .writeFd = -1};
  if (commandExists("nmcli")) {
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
  updateTitleXcb(x, root, active, utf8, netname, cfg.titleMax, &state, &cfg);
  char report[PANEL_TEXT_MAX] = "", action[1024] = "";
  size_t reportUsed = 0;
  unsigned ticks = 0;
  bool running = true, dirty = true, vd = false;
  while (running) {
    struct pollfd fds[] = {{tfd, POLLIN, 0},
                           {sfd, POLLIN, 0},
                           {bsp.readFd, POLLIN, 0},
                           {-1, 0, 0},
                           {xfd, POLLIN, 0},
                           {networkEvents.readFd, POLLIN, 0},
                           {titleRoot.readFd, POLLIN, 0},
                           {titleWindow.readFd, POLLIN, 0}};
    if (poll(fds, 8, -1) < 0) {
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
      updateTitleXcb(
          x, root, active, utf8, netname, cfg.titleMax, &state, &cfg);
      moduleScreencast(&cfg, &state, runtime);
      if (ticks % 5 == 0)
        moduleCpu(&cfg, &state);
      if (ticks % 5 == 0)
        moduleVolume(&cfg, &state);
      if (ticks % 10 == 0)
        moduleBattery(&cfg, &state);
      if (ticks % cfg.networkInterval == 0)
        moduleNetwork(&cfg, &state);
      if (ticks % cfg.weatherInterval == 0 && weatherPid <= 0)
        weatherPid = startWeatherRefresh(&cfg);
      if (networkEvents.pid <= 0 && commandExists("nmcli")) {
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
              dirty = true;
            } else if (reaped == bsp.pid) {
              logMessage("ERROR",
                         "bspc report subscription exited: status=%d",
                         WIFEXITED(status) ? WEXITSTATUS(status) : -1);
              bsp.pid = 0;
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
    if (fds[2].revents & POLLIN) {
      ssize_t n = read(
          bsp.readFd, report + reportUsed, sizeof(report) - reportUsed - 1);
      if (n > 0) {
        reportUsed += (size_t)n;
        report[reportUsed] = '\0';
        char *line = report;
        char *end = report + reportUsed;
        char *newline;
        while ((newline = memchr(line, '\n', (size_t)(end - line)))) {
          *newline = '\0';
          if (*line) {
            moduleWorkspace(&cfg, &state, line);
            updateTitleXcb(
                x, root, active, utf8, netname, cfg.titleMax, &state, &cfg);
            dirty = true;
          }
          line = newline + 1;
        }
        reportUsed = (size_t)(end - line);
        memmove(report, line, reportUsed);
        report[reportUsed] = '\0';
      }
      if (reportUsed == sizeof(report) - 1) {
        logMessage("ERROR", "discarding oversized bspwm report");
        reportUsed = 0;
        report[0] = '\0';
      }
    }
    if (fds[4].revents & POLLIN) {
      xcb_generic_event_t *ev;
      while ((ev = xcb_poll_for_event(x))) {
        bool redraw = false;
        if (nativePanelHandleEvent(
                panel, ev, action, sizeof(action), &redraw)) {
          doAction(&cfg, &state, action, &vd);
          dirty = true;
        }
        if ((ev->response_type & 0x7fU) == XCB_PROPERTY_NOTIFY) {
          updateTitleXcb(
              x, root, active, utf8, netname, cfg.titleMax, &state, &cfg);
          dirty = true;
        }
        if (redraw)
          dirty = true;
        free(ev);
      }
      if (vd) {
        moduleVolume(&cfg, &state);
        vd = false;
        dirty = true;
      }
    }
    if (fds[5].revents & POLLIN) {
      char discard[2048];
      while (read(networkEvents.readFd, discard, sizeof(discard)) > 0) {
      }
      moduleNetwork(&cfg, &state);
      dirty = true;
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
    if (dirty) {
      if (nativePanelDraw(panel, &state)) {
        logMessage("ERROR", "native panel rendering failed");
        running = false;
      }
      dirty = false;
    }
    if (bsp.pid <= 0 || kill(bsp.pid, 0)) {
      logMessage("ERROR", "bspc report subscription is unavailable");
      running = false;
    }
    if (xcb_connection_has_error(x)) {
      logMessage("ERROR", "X11 connection failed");
      running = false;
    }
  }
  close(tfd);
  close(sfd);
  if (weatherPid > 0) {
    kill(weatherPid, SIGTERM);
    waitpid(weatherPid, NULL, 0);
  }
  stopChild(&networkEvents);
  stopChild(&titleWindow);
  stopChild(&titleRoot);
  stopChild(&bsp);
  nativePanelDestroy(panel);
  xcb_disconnect(x);
  close(lock);
  return 0;
#endif
}
